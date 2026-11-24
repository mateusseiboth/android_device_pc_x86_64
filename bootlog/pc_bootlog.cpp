/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 *
 * pc-bootlog: the boot animation of LineageOS for PCs. Shows the live log (logcat -b all) as a
 * terminal: colors by priority, time since boot, the current stage, a progress bar driven by the
 * boot_progress_* events and the logo in the corner. Drawn on the CPU into a SurfaceFlinger layer
 * (libgui), so it starts with SurfaceFlinger, without ART.
 *
 * Runs as the "bootanim" service (pc-bootlog.rc). The drawing runs in a child process; when it
 * cannot draw or dies before the boot asked the animation to leave, this process becomes the stock
 * /system/bin/bootanimation (same pid, so SurfaceFlinger and WindowManager see no difference).
 * Leaves on service.bootanim.exit 0 -> 1 or boot_progress_enable_screen; never holds the boot.
 *
 * Usage: pc-bootlog            (from init)
 *        pc-bootlog --test     (draws for 15 s over the running system, for adb shell)
 *        androidboot.pc.bootlog=0 on the kernel command line runs the stock animation.
 *
 * Ported from the projector version: https://github.com/mateusseiboth/hy300-rk3566/tree/main/android/bootlog
 */
#define LOG_TAG "pc-bootlog"

#include <android-base/properties.h>
#include <android/native_window.h>
#include <binder/ProcessState.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <log/log.h>
#include <png.h>
#include <spawn.h>
#include <sys/wait.h>
#include <system/window.h>
#include <ui/DisplayMode.h>
#include <ui/LayerStack.h>
#include <ui/PixelFormat.h>
#include <ui/StaticDisplayInfo.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bootlog_model.h"
#include "bootlog_render.h"

extern char** environ;

namespace pc::bootlog {
namespace {

using android::sp;
using android::SurfaceComposerClient;
using namespace std::chrono_literals;

constexpr char kAssets[] = "/system_ext/etc/pc-bootlog/";
constexpr const char* kFontFiles[] = {"font-12x22.png", "font-18x32.png"};
constexpr char kLogoFile[] = "logo.png";
constexpr char kStockAnimation[] = "/system/bin/bootanimation";
constexpr char kLogcat[] = "/system/bin/logcat";
constexpr char kExitProp[] = "service.bootanim.exit";
constexpr char kEnabledProp[] = "ro.boot.pc.bootlog";
constexpr auto kFramePeriod = 66ms;
constexpr auto kTestTime = 15s;
// Only a guard against a renderer that stopped making progress: the stock animation takes over.
constexpr auto kBootTime = 10min;
constexpr int32_t kTopLayer = 0x40000000;

// Exit codes of the renderer: anything but kDrawn means the stock animation takes over.
enum RendererCode : int {
    kDrawn = 0,
    kNoComposer = 3,
    kNoDisplay = 4,
    kNoFont = 5,
    kNoSurface = 6,
    kLockFailed = 7,
    kTimedOut = 8,
};

int fail(RendererCode code, const char* what) {
    ALOGE("%s (code %d)", what, code);
    return code;
}

bool isExitRequested() {
    return android::base::GetProperty(kExitProp, "0") == "1";
}

int64_t uptimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

struct Decoded {
    int width = 0, height = 0;
    std::vector<uint8_t> bytes;
};

Decoded readPng(const std::string& path, png_uint_32 format) {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) return {};
    image.format = format;
    Decoded out{static_cast<int>(image.width), static_cast<int>(image.height),
                std::vector<uint8_t>(PNG_IMAGE_SIZE(image))};
    if (!png_image_finish_read(&image, nullptr, out.bytes.data(), 0, nullptr)) return {};
    return out;
}

std::vector<GrayImage> readFontStrips() {
    std::vector<GrayImage> strips;
    for (const char* file : kFontFiles) {
        Decoded png = readPng(std::string(kAssets) + file, PNG_FORMAT_GRAY);
        if (png.bytes.empty()) continue;
        strips.push_back(GrayImage{png.width, png.height, std::move(png.bytes)});
    }
    return strips;
}

Image readLogo(int size) {
    Decoded png = readPng(std::string(kAssets) + kLogoFile, PNG_FORMAT_RGBA);
    return scaleToFit(Image{png.width, png.height, std::move(png.bytes)}, size);
}

struct Display {
    sp<android::IBinder> token;
    int width, height;
    float density;
};

std::optional<Display> findDisplay() {
    const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
    if (ids.empty()) return std::nullopt;
    const sp<android::IBinder> token = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
    android::ui::DisplayMode mode;
    if (token == nullptr || SurfaceComposerClient::getActiveDisplayMode(token, &mode) != android::NO_ERROR)
        return std::nullopt;
    android::ui::StaticDisplayInfo info;
    const bool hasInfo = SurfaceComposerClient::getStaticDisplayInfo(ids.front().value, &info) == android::NO_ERROR;
    return Display{token, mode.resolution.getWidth(), mode.resolution.getHeight(), hasInfo ? info.density : 0.f};
}

std::optional<Font> pickFont(const Display& display) {
    std::vector<GrayImage> strips = readFontStrips();
    if (strips.empty()) return std::nullopt;
    std::vector<int> cellHeights;
    for (const GrayImage& strip : strips) cellHeights.push_back(strip.height / 2);
    const FontChoice choice = chooseFont(display.width, display.height, display.density, cellHeights);
    return Font::fromStrip(std::move(strips[choice.index]), choice.scale);
}

// logcat -b all -v brief, stdout and stderr into one pipe; no shell in between.
FILE* spawnLogcat() {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return nullptr;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    const char* argv[] = {"logcat", "-b", "all", "-v", "brief", nullptr};
    pid_t pid;
    const int rc = posix_spawn(&pid, kLogcat, &actions, nullptr, const_cast<char**>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc == 0) return fdopen(fds[0], "r");
    close(fds[0]);
    return nullptr;
}

class Screen {
  public:
    static std::optional<Screen> create(const sp<SurfaceComposerClient>& client, const Display& display) {
        const sp<android::SurfaceControl> control =
                client->createSurface(android::String8("pc-bootlog"), display.width, display.height,
                                      android::PIXEL_FORMAT_RGBX_8888, android::ISurfaceComposerClient::eOpaque);
        if (!android::SurfaceControl::isValid(control)) return std::nullopt;
        SurfaceComposerClient::Transaction t;
        t.setDisplayLayerStack(display.token, android::ui::DEFAULT_LAYER_STACK);
        t.setLayerStack(control, android::ui::DEFAULT_LAYER_STACK).setLayer(control, kTopLayer).show(control).apply();
        const sp<android::Surface> surface = control->getSurface();
        native_window_api_connect(surface.get(), NATIVE_WINDOW_API_CPU);
        return Screen(control, surface);
    }

    template <typename Draw>
    bool drawFrame(Draw&& draw) {
        ANativeWindow_Buffer buffer;
        if (surface_->lock(&buffer, nullptr) != android::NO_ERROR) return false;
        Canvas canvas(static_cast<uint32_t*>(buffer.bits), buffer.width, buffer.height, buffer.stride);
        draw(canvas);
        surface_->unlockAndPost();
        return true;
    }

    void hide() { SurfaceComposerClient::Transaction().hide(control_).apply(); }

  private:
    Screen(sp<android::SurfaceControl> control, sp<android::Surface> surface)
        : control_(std::move(control)), surface_(std::move(surface)) {}

    sp<android::SurfaceControl> control_;
    sp<android::Surface> surface_;
};

class Renderer {
  public:
    Renderer(Screen screen, Font font, Layout layout, Image logo, Language language)
        : screen_(std::move(screen)), font_(std::move(font)), layout_(layout), logo_(std::move(logo)), language_(language) {}

    int run(bool test) {
        // The reader thread owns its own reference: the process leaves with _exit, without
        // waiting for it (the projector version crashed when it used a model on main's stack).
        if (FILE* log = spawnLogcat()) startLogReader(model_, log);
        ExitWatch watch(isExitRequested());
        const auto deadline = std::chrono::steady_clock::now() + (test ? kTestTime : kBootTime);
        while (test || !watch.shouldExit(isExitRequested(), model_->screenEnabled())) {
            if (std::chrono::steady_clock::now() > deadline) return finish(test ? kDrawn : kTimedOut);
            if (!screen_.drawFrame([this](Canvas& canvas) { drawOnce(canvas); })) return fail(kLockFailed, "cannot lock the surface");
            std::this_thread::sleep_for(kFramePeriod);
        }
        return finish(kDrawn);
    }

  private:
    void drawOnce(Canvas& canvas) {
        shownProgress_ = stepProgress(shownProgress_, model_->progress());
        const FrameText text{std::string(title(language_)),
                             formatClock(uptimeMs(), stageName(model_->stage(), language_))};
        pc::bootlog::drawFrame(canvas, layout_, font_, model_->lastLines(layout_.logRows), text, logo_, shownProgress_);
    }

    int finish(RendererCode code) {
        screen_.hide();
        return code;
    }

    Screen screen_;
    Font font_;
    Layout layout_;
    Image logo_;
    Language language_;
    std::shared_ptr<BootModel> model_ = std::make_shared<BootModel>();
    int shownProgress_ = 0;
};

Language systemLanguage() {
    return pickLanguage(android::base::GetProperty("persist.sys.locale", ""),
                        android::base::GetProperty("ro.product.locale", ""));
}

int runRenderer(bool test) {
    android::ProcessState::self()->startThreadPool();
    const sp<SurfaceComposerClient> client = sp<SurfaceComposerClient>::make();
    if (client->initCheck() != android::NO_ERROR) return fail(kNoComposer, "no SurfaceFlinger");
    const std::optional<Display> display = findDisplay();
    if (!display) return fail(kNoDisplay, "no display");
    std::optional<Font> font = pickFont(*display);
    if (!font) return fail(kNoFont, "no font");
    std::optional<Screen> screen = Screen::create(client, *display);
    if (!screen) return fail(kNoSurface, "cannot create the surface");
    const Layout layout = makeLayout(display->width, display->height, font->cellWidth(), font->cellHeight());
    Image logo = readLogo(layout.logo.width());
    ALOGI("drawing on %dx%d (density %.2f), %dx%d cells", display->width, display->height, display->density,
          font->cellWidth(), font->cellHeight());
    return Renderer(std::move(*screen), std::move(*font), layout, std::move(logo), systemLanguage()).run(test);
}

// Becomes the stock boot animation, keeping the pid init and WindowManager watch.
int runStockAnimation() {
    ALOGI("running the stock boot animation");
    execl(kStockAnimation, "bootanimation", nullptr);
    ALOGE("cannot run %s: %s", kStockAnimation, strerror(errno));
    return 1;
}

int done() {
    return 0;
}

RendererEnd waitForRenderer(pid_t child) {
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) continue;
    ALOGI("renderer ended, status 0x%x", status);
    return rendererEndFromStatus(WIFEXITED(status), WEXITSTATUS(status), WIFSIGNALED(status));
}

// Indexed by NextStep.
constexpr int (*kNextSteps[])() = {done, runStockAnimation};

int supervise() {
    if (android::base::GetProperty(kEnabledProp, "1") == "0") return runStockAnimation();
    // Fork before anything touches binder: the child owns the SurfaceFlinger connection.
    const pid_t child = fork();
    if (child == 0) _exit(runRenderer(false));
    if (child < 0) return runStockAnimation();
    const NextStep next = decideAfterRenderer(waitForRenderer(child), isExitRequested());
    return kNextSteps[static_cast<size_t>(next)]();
}

}  // namespace
}  // namespace pc::bootlog

int main(int argc, char** argv) {
    const bool test = argc > 1 && std::string_view(argv[1]) == "--test";
    const int code = test ? pc::bootlog::runRenderer(true) : pc::bootlog::supervise();
    // _exit: no global destructors while the log reader thread still sits in fgets.
    fflush(stdout);
    _exit(code);
}

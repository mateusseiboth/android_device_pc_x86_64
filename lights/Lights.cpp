// The display's backlight for Android's brightness: the first device of /sys/class/backlight, picked as
// the kernel documents (firmware, then platform, then raw), scaled from Android's 0-255.
#include <aidl/android/hardware/light/BnLights.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <dirent.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

using aidl::android::hardware::light::BnLights;
using aidl::android::hardware::light::HwLight;
using aidl::android::hardware::light::HwLightEffect;
using aidl::android::hardware::light::HwLightState;
using aidl::android::hardware::light::LightType;

namespace {

constexpr char kBacklights[] = "/sys/class/backlight";
constexpr int kBacklightId = 0;
constexpr std::array<const char*, 3> kTypeOrder = {"firmware", "platform", "raw"};

std::string readValue(const std::string& path) {
    std::string text;
    android::base::ReadFileToString(path, &text);
    return android::base::Trim(text);
}

std::vector<std::string> listBacklights() {
    std::vector<std::string> names;
    DIR* dir = opendir(kBacklights);
    if (!dir) return names;
    for (dirent* e = readdir(dir); e; e = readdir(dir)) {
        if (e->d_name[0] != '.') names.emplace_back(e->d_name);
    }
    closedir(dir);
    return names;
}

std::optional<std::string> findBacklight() {
    std::vector<std::string> names = listBacklights();
    for (const char* type : kTypeOrder) {
        for (const std::string& name : names) {
            std::string path = std::string(kBacklights) + "/" + name;
            if (readValue(path + "/type") == type) return path;
        }
    }
    return std::nullopt;
}

// Android sends the brightness as a gray ARGB color: any channel is the 0-255 level.
int scaleToBacklight(int color, int maxBrightness) {
    int level = color & 0xff;
    int value = (level * maxBrightness + 127) / 255;
    return level > 0 && value == 0 ? 1 : value;
}

class Lights : public BnLights {
public:
    explicit Lights(std::optional<std::string> backlight) : backlight_(std::move(backlight)) {
        if (backlight_) maxBrightness_ = std::stoi("0" + readValue(*backlight_ + "/max_brightness"));
    }

    ndk::ScopedAStatus setLightState(int id, const HwLightState& state) override {
        if (id != kBacklightId || !backlight_ || maxBrightness_ <= 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        std::string value = std::to_string(scaleToBacklight(state.color, maxBrightness_));
        if (!android::base::WriteStringToFile(value, *backlight_ + "/brightness")) {
            PLOG(ERROR) << "cannot write " << *backlight_ << "/brightness";
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getLights(std::vector<HwLight>* lights) override {
        if (backlight_ && maxBrightness_ > 0) {
            lights->push_back({.id = kBacklightId, .ordinal = 0, .type = LightType::BACKLIGHT});
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setLightEffects(const std::vector<HwLightEffect>&) override {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

private:
    std::optional<std::string> backlight_;
    int maxBrightness_ = 0;
};

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto lights = ndk::SharedRefBase::make<Lights>(findBacklight());
    const std::string instance = std::string(Lights::descriptor) + "/default";
    if (AServiceManager_addService(lights->asBinder().get(), instance.c_str()) != STATUS_OK) {
        LOG(ERROR) << "cannot register " << instance;
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 1;
}

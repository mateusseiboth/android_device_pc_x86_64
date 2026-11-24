/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Boot log state, free of Android dependencies (tested on the host): the log lines, the boot stage
 * and progress from the boot_progress_* events, the end of the boot, the on-screen language and the
 * fallback decision when the renderer fails.
 *
 * Ported from the projector version: https://github.com/mateusseiboth/hy300-rk3566/tree/main/android/bootlog
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pc::bootlog {

enum class LineKind { kNormal, kError, kWarning, kKernel, kProgress, kRuntime };

struct Line {
    std::string text;
    LineKind kind;
};

LineKind classifyLine(std::string_view line);

enum class Stage {
    kKernel,
    kGraphics,
    kZygote,
    kPreload,
    kPreloaded,
    kSystemServer,
    kPackages,
    kScanningApps,
    kPackagesReady,
    kActivities,
    kDesktop,
};

inline constexpr std::array kAllStages = {
        Stage::kKernel,   Stage::kGraphics,      Stage::kZygote,       Stage::kPreload,
        Stage::kPreloaded, Stage::kSystemServer, Stage::kPackages,     Stage::kScanningApps,
        Stage::kPackagesReady, Stage::kActivities, Stage::kDesktop,
};

enum class Language { kEnglish, kPortuguese };

// persist.sys.locale (the user's choice) first, then ro.product.locale.
Language pickLanguage(std::string_view persistLocale, std::string_view productLocale);
std::string_view stageName(Stage stage, Language language);
std::string_view title(Language language);
std::string formatClock(int64_t uptimeMs, std::string_view stage);

// Moves 1/6 of the way to the target (at least 1) without passing it; never goes back.
int stepProgress(int shown, int target);

// The boot animation ends when service.bootanim.exit goes from 0 to 1 (WindowManager, or
// SurfaceFlinger's bootFinished) or when boot_progress_enable_screen shows up in the log. A value
// already at 1 when we start is stale and only counts after it went back to 0.
class ExitWatch {
  public:
    explicit ExitWatch(bool exitPropAtStart) : sawZero_(!exitPropAtStart) {}
    bool shouldExit(bool exitProp, bool screenEnabled);

  private:
    bool sawZero_;
};

class BootModel {
  public:
    explicit BootModel(size_t maxLines = 400) : maxLines_(maxLines) {}

    void addLine(std::string line);
    std::vector<Line> lastLines(size_t count) const;
    Stage stage() const;
    int progress() const;
    bool screenEnabled() const;

  private:
    void applyStages(std::string_view line);

    mutable std::mutex lock_;
    size_t maxLines_;
    std::deque<Line> lines_;
    Stage stage_ = Stage::kKernel;
    int progress_ = 0;
    bool screenEnabled_ = false;
};

// Reads lines from "in" on a detached thread until end of file and hands them to the model. The
// thread holds its own reference to the model: the caller may leave without waiting for it.
void startLogReader(std::shared_ptr<BootModel> model, FILE* in);

// The renderer runs in a child process; when it fails the service turns into the stock animation,
// unless the boot already asked the animation to leave.
enum class RendererEnd { kClean, kFailed };
enum class NextStep { kDone, kStockAnimation };

RendererEnd rendererEndFromStatus(bool exited, int exitCode, bool signaled);
NextStep decideAfterRenderer(RendererEnd end, bool exitRequested);

}  // namespace pc::bootlog

/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bootlog_model.h"

#include <algorithm>
#include <thread>

namespace pc::bootlog {

namespace {

struct StageEvent {
    std::string_view event;
    int progress;
    Stage stage;
};

// Same table as the projector version (its Main.java), with the stages named for the screen.
constexpr StageEvent kStageEvents[] = {
        {"boot_progress_start", 8, Stage::kZygote},
        {"boot_progress_preload_start", 15, Stage::kPreload},
        {"boot_progress_preload_end", 40, Stage::kPreloaded},
        {"boot_progress_system_run", 50, Stage::kSystemServer},
        {"boot_progress_pms_start", 58, Stage::kPackages},
        {"boot_progress_pms_data_scan_start", 68, Stage::kScanningApps},
        {"boot_progress_pms_ready", 78, Stage::kPackagesReady},
        {"boot_progress_ams_ready", 90, Stage::kActivities},
        {"boot_progress_enable_screen", 100, Stage::kDesktop},
};

constexpr std::string_view kEnableScreen = "boot_progress_enable_screen";
constexpr int kGraphicsProgress = 4;

struct StageNames {
    std::string_view english;
    std::string_view portuguese;
};

// Indexed by Stage. Short, as they share the header line with the clock.
constexpr StageNames kStageNames[] = {
        {"kernel", "kernel"},
        {"graphics", "gr\xc3\xa1" "ficos"},
        {"zygote", "zygote"},
        {"preloading", "pr\xc3\xa9-carregando"},
        {"preload done", "pr\xc3\xa9-carga pronta"},
        {"system server", "servidor do sistema"},
        {"package manager", "gerenciador de pacotes"},
        {"scanning apps", "verificando apps"},
        {"packages ready", "pacotes prontos"},
        {"activity manager", "gerenciador de atividades"},
        {"opening the desktop", "abrindo a \xc3\xa1rea de trabalho"},
};
static_assert(std::size(kStageNames) == kAllStages.size());

constexpr StageNames kTitle = {"LineageOS for PCs :: booting", "LineageOS para PCs :: iniciando"};

std::string_view pickName(const StageNames& names, Language language) {
    return language == Language::kPortuguese ? names.portuguese : names.english;
}

bool contains(std::string_view line, std::string_view needle) {
    return line.find(needle) != std::string_view::npos;
}

// "-v brief": "P/Tag(pid): message", P the priority letter.
struct PriorityKind {
    char priority;
    LineKind kind;
};

constexpr PriorityKind kPriorityKinds[] = {
        {'E', LineKind::kError},
        {'F', LineKind::kError},
        {'W', LineKind::kWarning},
};

struct SourceRule {
    bool (*matches)(std::string_view line);
    LineKind kind;
};

constexpr SourceRule kSourceRules[] = {
        {[](std::string_view l) { return contains(l, "/kernel") || contains(l, " kernel") || l.starts_with("[k]"); },
         LineKind::kKernel},
        {[](std::string_view l) { return contains(l, "boot_progress"); }, LineKind::kProgress},
        {[](std::string_view l) { return contains(l, "Zygote") || contains(l, "SystemServer"); }, LineKind::kRuntime},
};

const PriorityKind* findPriority(std::string_view line) {
    if (line.size() < 2 || line[1] != '/') return nullptr;
    const auto* found = std::find_if(std::begin(kPriorityKinds), std::end(kPriorityKinds),
                                     [&](const PriorityKind& p) { return p.priority == line[0]; });
    return found == std::end(kPriorityKinds) ? nullptr : found;
}

std::string clean(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    std::replace(line.begin(), line.end(), '\t', ' ');
    return line;
}

bool isPortuguese(std::string_view locale) {
    return locale.starts_with("pt") && (locale.size() == 2 || locale[2] == '-' || locale[2] == '_');
}

}  // namespace

LineKind classifyLine(std::string_view line) {
    if (const PriorityKind* priority = findPriority(line)) return priority->kind;
    const auto* rule = std::find_if(std::begin(kSourceRules), std::end(kSourceRules),
                                    [&](const SourceRule& r) { return r.matches(line); });
    return rule == std::end(kSourceRules) ? LineKind::kNormal : rule->kind;
}

Language pickLanguage(std::string_view persistLocale, std::string_view productLocale) {
    const std::string_view locale = persistLocale.empty() ? productLocale : persistLocale;
    return isPortuguese(locale) ? Language::kPortuguese : Language::kEnglish;
}

std::string_view stageName(Stage stage, Language language) {
    return pickName(kStageNames[static_cast<size_t>(stage)], language);
}

std::string_view title(Language language) {
    return pickName(kTitle, language);
}

std::string formatClock(int64_t uptimeMs, std::string_view stage) {
    char clock[32];
    snprintf(clock, sizeof(clock), "%.2fs  ", static_cast<double>(uptimeMs) / 1000.0);
    return std::string(clock).append(stage);
}

int stepProgress(int shown, int target) {
    if (shown >= target) return shown;
    return std::min(target, shown + std::max(1, (target - shown) / 6));
}

bool ExitWatch::shouldExit(bool exitProp, bool screenEnabled) {
    sawZero_ = sawZero_ || !exitProp;
    return screenEnabled || (sawZero_ && exitProp);
}

void BootModel::addLine(std::string line) {
    line = clean(std::move(line));
    const LineKind kind = classifyLine(line);
    std::lock_guard<std::mutex> guard(lock_);
    applyStages(line);
    lines_.push_back(Line{std::move(line), kind});
    while (lines_.size() > maxLines_) lines_.pop_front();
}

void BootModel::applyStages(std::string_view line) {
    for (const StageEvent& s : kStageEvents) {
        if (!contains(line, s.event)) continue;
        progress_ = s.progress;
        stage_ = s.stage;
    }
    screenEnabled_ = screenEnabled_ || contains(line, kEnableScreen);
    if (progress_ != 0 || !contains(line, "SurfaceFlinger")) return;
    progress_ = kGraphicsProgress;
    stage_ = Stage::kGraphics;
}

std::vector<Line> BootModel::lastLines(size_t count) const {
    std::lock_guard<std::mutex> guard(lock_);
    const size_t start = lines_.size() > count ? lines_.size() - count : 0;
    return std::vector<Line>(lines_.begin() + start, lines_.end());
}

Stage BootModel::stage() const {
    std::lock_guard<std::mutex> guard(lock_);
    return stage_;
}

int BootModel::progress() const {
    std::lock_guard<std::mutex> guard(lock_);
    return progress_;
}

bool BootModel::screenEnabled() const {
    std::lock_guard<std::mutex> guard(lock_);
    return screenEnabled_;
}

void startLogReader(std::shared_ptr<BootModel> model, FILE* in) {
    std::thread([model = std::move(model), in]() {
        char line[1024];
        while (fgets(line, sizeof(line), in)) model->addLine(line);
        fclose(in);
    }).detach();
}

RendererEnd rendererEndFromStatus(bool exited, int exitCode, bool signaled) {
    return exited && !signaled && exitCode == 0 ? RendererEnd::kClean : RendererEnd::kFailed;
}

NextStep decideAfterRenderer(RendererEnd end, bool exitRequested) {
    return end == RendererEnd::kClean || exitRequested ? NextStep::kDone : NextStep::kStockAnimation;
}

}  // namespace pc::bootlog

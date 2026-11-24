/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host tests for the boot log model: stages and progress from the boot_progress_* events, the end of
 * the boot, line colors, the on-screen language and what happens when the renderer fails.
 * Run: python3 test_bootlog.py (host compiler), or atest pc-bootlog-model-test.
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "bootlog_model.h"

namespace pc::bootlog {
namespace {

TEST(BootModel, startsInTheKernelStage) {
    BootModel model;
    EXPECT_EQ(model.stage(), Stage::kKernel);
    EXPECT_EQ(model.progress(), 0);
    EXPECT_FALSE(model.screenEnabled());
}

TEST(BootModel, bootProgressEventsAdvanceTheStage) {
    BootModel model;
    model.addLine("I/boot_progress_preload_end(  123): 4567\n");
    EXPECT_EQ(model.stage(), Stage::kPreloaded);
    EXPECT_EQ(model.progress(), 40);
    model.addLine("I/boot_progress_ams_ready(  456): 9999\n");
    EXPECT_EQ(model.stage(), Stage::kActivities);
    EXPECT_EQ(model.progress(), 90);
}

TEST(BootModel, theDataScanIsItsOwnStage) {
    BootModel model;
    model.addLine("I/boot_progress_pms_data_scan_start(  456): 9999\n");
    EXPECT_EQ(model.stage(), Stage::kScanningApps);
    EXPECT_EQ(model.progress(), 68);
}

TEST(BootModel, surfaceFlingerGivesTheFirstProgress) {
    BootModel model;
    model.addLine("I/SurfaceFlinger(  200): Enter boot animation\n");
    EXPECT_EQ(model.stage(), Stage::kGraphics);
    EXPECT_EQ(model.progress(), 4);
}

TEST(BootModel, surfaceFlingerNeverMovesProgressBack) {
    BootModel model;
    model.addLine("I/boot_progress_start(  1): 5\n");
    model.addLine("I/SurfaceFlinger(  200): something late\n");
    EXPECT_EQ(model.stage(), Stage::kZygote);
    EXPECT_EQ(model.progress(), 8);
}

TEST(BootModel, enableScreenEndsTheBoot) {
    BootModel model;
    model.addLine("I/boot_progress_enable_screen(  700): 21000\n");
    EXPECT_TRUE(model.screenEnabled());
    EXPECT_EQ(model.progress(), 100);
    EXPECT_EQ(model.stage(), Stage::kDesktop);
}

TEST(BootModel, keepsOnlyTheLastLinesWithoutNewlinesOrTabs) {
    BootModel model(3);
    for (int i = 0; i < 5; ++i) model.addLine("I/Tag(1):\tline " + std::to_string(i) + "\r\n");
    auto lines = model.lastLines(10);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines.front().text, "I/Tag(1): line 2");
    EXPECT_EQ(lines.back().text, "I/Tag(1): line 4");
    EXPECT_EQ(model.lastLines(2).front().text, "I/Tag(1): line 3");
}

TEST(ClassifyLine, prioritiesAndSources) {
    EXPECT_EQ(classifyLine("E/Tag(1): x"), LineKind::kError);
    EXPECT_EQ(classifyLine("F/Tag(1): x"), LineKind::kError);
    EXPECT_EQ(classifyLine("W/Tag(1): x"), LineKind::kWarning);
    EXPECT_EQ(classifyLine("I/kernel(  0): x"), LineKind::kKernel);
    EXPECT_EQ(classifyLine("I/boot_progress_start(  1): 5"), LineKind::kProgress);
    EXPECT_EQ(classifyLine("I/Zygote(  2): x"), LineKind::kRuntime);
    EXPECT_EQ(classifyLine("I/SystemServer(  2): x"), LineKind::kRuntime);
    EXPECT_EQ(classifyLine("I/Tag(1): x"), LineKind::kNormal);
    EXPECT_EQ(classifyLine(""), LineKind::kNormal);
}

TEST(ClassifyLine, anErrorFromTheKernelIsStillAnError) {
    EXPECT_EQ(classifyLine("E/kernel(  0): oops"), LineKind::kError);
}

TEST(StepProgress, approachesTheTargetWithoutPassingIt) {
    EXPECT_EQ(stepProgress(0, 0), 0);
    EXPECT_EQ(stepProgress(0, 60), 10);
    EXPECT_EQ(stepProgress(98, 100), 99);
    EXPECT_EQ(stepProgress(100, 40), 100);
}

TEST(PickLanguage, portugueseForAnyPortugueseLocale) {
    EXPECT_EQ(pickLanguage("pt-BR", ""), Language::kPortuguese);
    EXPECT_EQ(pickLanguage("pt-PT", "en-US"), Language::kPortuguese);
    EXPECT_EQ(pickLanguage("", "pt-BR"), Language::kPortuguese);
}

TEST(PickLanguage, theUsersChoiceBeatsTheProductDefault) {
    EXPECT_EQ(pickLanguage("en-US", "pt-BR"), Language::kEnglish);
}

TEST(PickLanguage, englishOtherwise) {
    EXPECT_EQ(pickLanguage("", ""), Language::kEnglish);
    EXPECT_EQ(pickLanguage("de-DE", ""), Language::kEnglish);
    EXPECT_EQ(pickLanguage("ptx", ""), Language::kEnglish);
}

TEST(StageName, everyStageHasBothLanguages) {
    for (Stage stage : kAllStages) {
        EXPECT_FALSE(stageName(stage, Language::kEnglish).empty());
        EXPECT_FALSE(stageName(stage, Language::kPortuguese).empty());
    }
    EXPECT_EQ(stageName(Stage::kSystemServer, Language::kEnglish), "system server");
    EXPECT_EQ(stageName(Stage::kSystemServer, Language::kPortuguese), "servidor do sistema");
}

TEST(OnScreenText, neverUsesTheEmDash) {
    const std::string emDash = "\xe2\x80\x94";
    for (Language language : {Language::kEnglish, Language::kPortuguese}) {
        EXPECT_EQ(title(language).find(emDash), std::string::npos);
        for (Stage stage : kAllStages) EXPECT_EQ(stageName(stage, language).find(emDash), std::string::npos);
    }
}

TEST(Title, namesTheSystemInTheLanguage) {
    EXPECT_EQ(title(Language::kEnglish), "LineageOS for PCs :: booting");
    EXPECT_EQ(title(Language::kPortuguese), "LineageOS para PCs :: iniciando");
}

TEST(FormatClock, secondsSinceBootAndTheStage) {
    EXPECT_EQ(formatClock(12345, "zygote"), "12.35s  zygote");
    EXPECT_EQ(formatClock(0, "kernel"), "0.00s  kernel");
}

TEST(ExitWatch, exitsWhenThePropertyGoesFromZeroToOne) {
    ExitWatch watch(false);
    EXPECT_FALSE(watch.shouldExit(false, false));
    EXPECT_TRUE(watch.shouldExit(true, false));
}

TEST(ExitWatch, ignoresAPropertyThatWasAlreadyOne) {
    // Left at 1 by an earlier boot animation (a restart of SurfaceFlinger resets it to 0 first).
    ExitWatch watch(true);
    EXPECT_FALSE(watch.shouldExit(true, false));
    EXPECT_FALSE(watch.shouldExit(false, false));
    EXPECT_TRUE(watch.shouldExit(true, false));
}

TEST(ExitWatch, exitsWhenTheScreenIsEnabled) {
    ExitWatch watch(false);
    EXPECT_TRUE(watch.shouldExit(false, true));
}

TEST(AfterRenderer, aCleanExitEndsTheService) {
    EXPECT_EQ(decideAfterRenderer(RendererEnd::kClean, false), NextStep::kDone);
}

TEST(AfterRenderer, aFailureFallsBackToTheStockAnimation) {
    EXPECT_EQ(decideAfterRenderer(RendererEnd::kFailed, false), NextStep::kStockAnimation);
}

TEST(AfterRenderer, aFailureAfterTheBootFinishedNeedsNothingMore) {
    EXPECT_EQ(decideAfterRenderer(RendererEnd::kFailed, true), NextStep::kDone);
}

TEST(RendererEndFromStatus, onlyExitCodeZeroIsClean) {
    EXPECT_EQ(rendererEndFromStatus(true, 0, false), RendererEnd::kClean);
    EXPECT_EQ(rendererEndFromStatus(true, 3, false), RendererEnd::kFailed);
    EXPECT_EQ(rendererEndFromStatus(false, 0, true), RendererEnd::kFailed);
}

// The log reader thread is detached and owns a reference to the model: the projector version
// crashed on exit when the thread used a model that lived on main's stack.
TEST(LogReader, keepsTheModelAliveAfterItsOwnerDropsIt) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    FILE* in = fdopen(fds[0], "r");
    ASSERT_NE(in, nullptr);
    auto model = std::make_shared<BootModel>();
    std::weak_ptr<BootModel> watch = model;
    startLogReader(model, in);
    model.reset();
    EXPECT_FALSE(watch.expired());

    const char line[] = "I/boot_progress_enable_screen(  1): 2\n";
    ASSERT_EQ(write(fds[1], line, sizeof(line) - 1), static_cast<ssize_t>(sizeof(line) - 1));
    for (int i = 0; i < 200; ++i) {
        auto alive = watch.lock();
        if (alive && alive->screenEnabled()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto alive = watch.lock();
    ASSERT_NE(alive, nullptr);
    EXPECT_TRUE(alive->screenEnabled());
    alive.reset();
    close(fds[1]);
    for (int i = 0; i < 200 && !watch.expired(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(watch.expired());  // end of the pipe: the thread leaves and drops the model
}

}  // namespace
}  // namespace pc::bootlog

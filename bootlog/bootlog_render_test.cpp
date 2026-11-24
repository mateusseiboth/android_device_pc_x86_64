/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host tests for the boot log drawing: the pixel buffer, the logo scaling, the bitmap font, the
 * layout for PC screens and a whole frame.
 */
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "bootlog_render.h"

namespace pc::bootlog {
namespace {

struct Pixels {
    Pixels(int width, int height, int stride) : buffer(stride * height, 0xdeadbeef), canvas(buffer.data(), width, height, stride) {}
    std::vector<uint32_t> buffer;
    Canvas canvas;
};

TEST(Canvas, fillWritesRgbxAndStaysInside) {
    Pixels px(4, 3, 6);
    px.canvas.fill({-5, -5, 2, 2}, {10, 20, 30});
    EXPECT_EQ(px.buffer[0], 0xff1e140au);  // R in the low byte, as RGBX_8888 in memory
    EXPECT_EQ(px.canvas.at(1, 1), (Rgb{10, 20, 30}));
    EXPECT_EQ(px.buffer[2], 0xdeadbeefu);
    EXPECT_EQ(px.buffer[4], 0xdeadbeefu);  // the stride's padding is never written
    px.canvas.fill({0, 0, 100, 100}, {1, 2, 3});
    EXPECT_EQ(px.buffer[4], 0xdeadbeefu);
    EXPECT_EQ(px.canvas.at(3, 2), (Rgb{1, 2, 3}));
}

TEST(Canvas, blendMixesByCoverage) {
    Pixels px(2, 1, 2);
    px.canvas.fill({0, 0, 2, 1}, {0, 0, 0});
    px.canvas.blend(0, 0, {200, 100, 50}, 255);
    EXPECT_EQ(px.canvas.at(0, 0), (Rgb{200, 100, 50}));
    px.canvas.blend(1, 0, {200, 100, 50}, 0);
    EXPECT_EQ(px.canvas.at(1, 0), (Rgb{0, 0, 0}));
    px.canvas.blend(1, 0, {200, 100, 50}, 128);
    EXPECT_EQ(px.canvas.at(1, 0), (Rgb{100, 50, 25}));
    px.canvas.blend(5, 5, {1, 1, 1}, 255);  // outside: ignored
}

Image solid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Image image{width, height, {}};
    for (int i = 0; i < width * height; ++i) image.rgba.insert(image.rgba.end(), {r, g, b, a});
    return image;
}

TEST(ScaleToFit, keepsTheAspectRatio) {
    const Image scaled = scaleToFit(solid(400, 200, 255, 0, 0, 255), 100);
    EXPECT_EQ(scaled.width, 100);
    EXPECT_EQ(scaled.height, 50);
    EXPECT_EQ(scaled.rgba.size(), 100u * 50u * 4u);
}

TEST(ScaleToFit, averagesTheArea) {
    Image checker{2, 2, {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255}};
    const Image scaled = scaleToFit(checker, 1);
    ASSERT_EQ(scaled.width, 1);
    EXPECT_NEAR(scaled.rgba[0], 128, 1);
    EXPECT_EQ(scaled.rgba[3], 255);
}

TEST(ScaleToFit, transparentPixelsDoNotDarkenTheEdges) {
    Image edge{2, 1, {255, 0, 0, 255, 0, 0, 0, 0}};
    const Image scaled = scaleToFit(edge, 1);
    EXPECT_EQ(scaled.rgba[0], 255);
    EXPECT_NEAR(scaled.rgba[3], 128, 1);
}

TEST(ScaleToFit, anEmptyImageStaysEmpty) {
    EXPECT_EQ(scaleToFit(Image{}, 100).width, 0);
}

TEST(DrawImage, blendsByAlpha) {
    Pixels px(3, 1, 3);
    px.canvas.fill({0, 0, 3, 1}, {0, 0, 0});
    drawImage(px.canvas, Image{2, 1, {255, 255, 255, 255, 255, 255, 255, 0}}, 1, 0);
    EXPECT_EQ(px.canvas.at(0, 0), (Rgb{0, 0, 0}));
    EXPECT_EQ(px.canvas.at(1, 0), (Rgb{255, 255, 255}));
    EXPECT_EQ(px.canvas.at(2, 0), (Rgb{0, 0, 0}));
}

// A 96-glyph strip (AOSP recovery's layout) with 2x3 cells: every glyph has one lit pixel in its
// top-left corner, 'A' and the bold '?' are fully lit.
GrayImage testStrip() {
    const int cw = 2, ch = 3;
    GrayImage strip{96 * cw, 2 * ch, std::vector<uint8_t>(96 * cw * 2 * ch, 0)};
    auto lit = [&](int glyph, int row, int x, int y) { strip.gray[(row * ch + y) * strip.width + glyph * cw + x] = 255; };
    for (int g = 0; g < 96; ++g) lit(g, 0, 0, 0);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            lit('A' - 0x20, 0, x, y);
            lit('?' - 0x20, 1, x, y);
        }
    return strip;
}

TEST(Font, rejectsAStripWithoutTheRecoveryLayout) {
    EXPECT_FALSE(Font::fromStrip(GrayImage{95, 6, std::vector<uint8_t>(95 * 6)}, 1).has_value());
    EXPECT_FALSE(Font::fromStrip(GrayImage{192, 5, std::vector<uint8_t>(192 * 5)}, 1).has_value());
    EXPECT_FALSE(Font::fromStrip(testStrip(), 0).has_value());
}

TEST(Font, cellsGrowWithTheScale) {
    const auto font = Font::fromStrip(testStrip(), 3);
    ASSERT_TRUE(font.has_value());
    EXPECT_EQ(font->cellWidth(), 6);
    EXPECT_EQ(font->cellHeight(), 9);
}

TEST(Font, drawsGlyphCoverageScaled) {
    const auto font = Font::fromStrip(testStrip(), 2);
    Pixels px(8, 6, 8);
    px.canvas.fill({0, 0, 8, 6}, {0, 0, 0});
    font->draw(px.canvas, 0, 0, "A.", {9, 9, 9}, Weight::kRegular, 10);
    EXPECT_EQ(px.canvas.at(3, 5), (Rgb{9, 9, 9}));  // 'A' fully lit, 2x
    EXPECT_EQ(px.canvas.at(4, 0), (Rgb{9, 9, 9}));  // '.' corner pixel, 2x2
    EXPECT_EQ(px.canvas.at(5, 1), (Rgb{9, 9, 9}));
    EXPECT_EQ(px.canvas.at(6, 0), (Rgb{0, 0, 0}));
    EXPECT_EQ(px.canvas.at(4, 2), (Rgb{0, 0, 0}));
}

TEST(Font, boldUsesTheSecondRowAndUnknownCharactersAreQuestionMarks) {
    const auto font = Font::fromStrip(testStrip(), 1);
    Pixels px(2, 3, 2);
    px.canvas.fill({0, 0, 2, 3}, {0, 0, 0});
    font->draw(px.canvas, 0, 0, "\x01", {7, 7, 7}, Weight::kBold, 10);
    EXPECT_EQ(px.canvas.at(1, 2), (Rgb{7, 7, 7}));
}

TEST(Font, stopsAtTheColumnLimit) {
    const auto font = Font::fromStrip(testStrip(), 1);
    Pixels px(6, 3, 6);
    px.canvas.fill({0, 0, 6, 3}, {0, 0, 0});
    font->draw(px.canvas, 0, 0, "AAA", {7, 7, 7}, Weight::kRegular, 2);
    EXPECT_EQ(px.canvas.at(3, 2), (Rgb{7, 7, 7}));
    EXPECT_EQ(px.canvas.at(4, 0), (Rgb{0, 0, 0}));
}

TEST(FoldToAscii, keepsAsciiAndDropsAccents) {
    EXPECT_EQ(foldToAscii("I/Tag(1): ok"), "I/Tag(1): ok");
    EXPECT_EQ(foldToAscii("gr\xc3\xa1" "ficos"), "graficos");
    EXPECT_EQ(foldToAscii("a\xc3\xa7\xc3\xa3o \xc3\x89"), "acao E");
    EXPECT_EQ(foldToAscii("\xe6\x97\xa5x"), "?x");
    EXPECT_EQ(foldToAscii("a\x01" "b"), "a b");
}

TEST(ChooseFont, fullHdUsesTheSmallFontAsIs) {
    EXPECT_EQ(chooseFont(1920, 1080, 0.f, {22, 32}), (FontChoice{0, 1}));
    EXPECT_EQ(chooseFont(1366, 768, 0.f, {22, 32}), (FontChoice{0, 1}));
}

TEST(ChooseFont, biggerScreensGetBiggerText) {
    EXPECT_EQ(chooseFont(3840, 2160, 0.f, {22, 32}), (FontChoice{0, 2}));
    EXPECT_EQ(chooseFont(5120, 3072, 0.f, {22, 32}), (FontChoice{1, 2}));
}

TEST(ChooseFont, aDenseScreenGetsBiggerText) {
    EXPECT_EQ(chooseFont(1920, 1080, 3.f, {22, 32}), (FontChoice{0, 2}));
}

TEST(ChooseFont, portraitScreensUseTheShortSide) {
    EXPECT_EQ(chooseFont(2160, 3840, 0.f, {22, 32}), (FontChoice{0, 2}));
}

bool inside(const Rect& r, int width, int height) {
    return r.left >= 0 && r.top >= 0 && r.right <= width && r.bottom <= height && r.left < r.right && r.top < r.bottom;
}

TEST(MakeLayout, fullHdShowsAFullTerminal) {
    const Layout layout = makeLayout(1920, 1080, 12, 22);
    EXPECT_GE(layout.logRows, 40);
    EXPECT_GE(layout.logCols, 150);
    EXPECT_LE(layout.logRows * 22, layout.log.height());
    EXPECT_LE(layout.logCols * 12, layout.log.width());
}

TEST(MakeLayout, everythingFitsInOrder) {
    for (auto [w, h, cw, ch] : {std::array{1920, 1080, 12, 22}, std::array{3840, 2160, 24, 44},
                                std::array{1024, 600, 12, 22}, std::array{1080, 1920, 12, 22}}) {
        const Layout layout = makeLayout(w, h, cw, ch);
        for (const Rect& r : {layout.header, layout.rule, layout.log, layout.logo, layout.bar}) EXPECT_TRUE(inside(r, w, h));
        EXPECT_LE(layout.header.bottom, layout.rule.top);
        EXPECT_LE(layout.rule.bottom, layout.log.top);
        EXPECT_LE(layout.log.bottom, layout.bar.top);
        EXPECT_LE(layout.logo.bottom, layout.bar.top);
        EXPECT_GE(layout.logo.top, layout.log.top);
        EXPECT_EQ(layout.logo.width(), layout.logo.height());
        EXPECT_GE(layout.logRows, 1);
    }
}

TEST(MakeLayout, theLogoGrowsWithTheScreen) {
    EXPECT_GT(makeLayout(3840, 2160, 24, 44).logo.width(), makeLayout(1920, 1080, 12, 22).logo.width());
}

TEST(Palette, everyKindHasItsColor) {
    const Palette& palette = defaultPalette();
    EXPECT_EQ(palette.forKind(LineKind::kError), (Rgb{255, 95, 110}));
    EXPECT_FALSE(palette.forKind(LineKind::kWarning) == palette.forKind(LineKind::kNormal));
    EXPECT_FALSE(palette.background == palette.forKind(LineKind::kNormal));
}

TEST(DrawFrame, drawsTheLogTheBarAndTheLogo) {
    const auto font = Font::fromStrip(testStrip(), 1);
    const int w = 200, h = 120;
    Pixels px(w, h, w);
    const Layout layout = makeLayout(w, h, font->cellWidth(), font->cellHeight());
    const Image logo = solid(layout.logo.width(), layout.logo.height(), 1, 2, 3, 255);
    const std::vector<Line> lines = {{"A", LineKind::kNormal}, {"A", LineKind::kError}};
    drawFrame(px.canvas, layout, *font, lines, FrameText{"A", "A"}, logo, 50);

    const Palette& palette = defaultPalette();
    EXPECT_EQ(px.canvas.at(0, h - 1), palette.background);
    EXPECT_EQ(px.canvas.at(layout.bar.left, layout.bar.top), palette.barFill);
    EXPECT_EQ(px.canvas.at(layout.bar.right - 1, layout.bar.top), palette.barTrack);
    EXPECT_EQ(px.canvas.at(layout.rule.left, layout.rule.top), palette.rule);
    EXPECT_EQ(px.canvas.at(layout.logo.left, layout.logo.top), (Rgb{1, 2, 3}));
    // The newest line is the last one: errors are red.
    EXPECT_EQ(px.canvas.at(layout.log.left, layout.log.top + font->cellHeight()), palette.forKind(LineKind::kError));
    EXPECT_EQ(px.canvas.at(layout.log.left, layout.log.top), palette.forKind(LineKind::kNormal));
}

TEST(DrawFrame, showsOnlyTheLinesThatFit) {
    const auto font = Font::fromStrip(testStrip(), 1);
    const int w = 200, h = 120;
    Pixels px(w, h, w);
    const Layout layout = makeLayout(w, h, font->cellWidth(), font->cellHeight());
    std::vector<Line> lines(layout.logRows + 5, Line{"A", LineKind::kNormal});
    lines.back().kind = LineKind::kWarning;
    drawFrame(px.canvas, layout, *font, lines, FrameText{"", ""}, Image{}, 0);
    const int lastTop = layout.log.top + (layout.logRows - 1) * font->cellHeight();
    EXPECT_EQ(px.canvas.at(layout.log.left, lastTop), defaultPalette().forKind(LineKind::kWarning));
    EXPECT_EQ(px.canvas.at(layout.bar.left, layout.bar.top), defaultPalette().barTrack);
}

}  // namespace
}  // namespace pc::bootlog

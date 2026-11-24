/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Boot log drawing on the CPU, free of Android dependencies (tested on the host): a pixel buffer as
 * Surface::lock hands it (RGBX_8888), the logo scaled once, a bitmap font cut from AOSP recovery's
 * glyph strips, the layout for PC screens and the whole frame.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bootlog_model.h"

namespace pc::bootlog {

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb&) const = default;
};

struct Rect {
    int left, top, right, bottom;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

class Canvas {
  public:
    Canvas(uint32_t* pixels, int width, int height, int stride)
        : pixels_(pixels), width_(width), height_(height), stride_(stride) {}

    int width() const { return width_; }
    int height() const { return height_; }
    void fill(Rect rect, Rgb color);
    void blend(int x, int y, Rgb color, uint8_t alpha);
    Rgb at(int x, int y) const;

  private:
    uint32_t* pixels_;
    int width_, height_, stride_;
};

// 8-bit RGBA, not premultiplied (as libpng's PNG_FORMAT_RGBA gives it).
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Area average into a size x size box, keeping the aspect ratio; alpha-weighted, so transparent
// pixels do not darken the edges.
Image scaleToFit(const Image& source, int size);
void drawImage(Canvas& canvas, const Image& image, int x, int y);

struct GrayImage {
    int width = 0, height = 0;
    std::vector<uint8_t> gray;
};

enum class Weight { kRegular, kBold };

// AOSP recovery's font images (bootable/recovery/fonts): the 96 characters 0x20..0x7f side by side,
// regular on the top row and bold on the bottom one. Drawn at an integer scale.
class Font {
  public:
    static std::optional<Font> fromStrip(GrayImage strip, int scale);

    int cellWidth() const { return glyphWidth_ * scale_; }
    int cellHeight() const { return glyphHeight_ * scale_; }
    // Draws ASCII text with its top-left corner at (x, top); other bytes are drawn as '?'.
    void draw(Canvas& canvas, int x, int top, std::string_view text, Rgb color, Weight weight, int maxCols) const;

  private:
    Font(GrayImage strip, int scale);
    void drawGlyph(Canvas& canvas, int x, int top, int glyph, int row, Rgb color) const;

    GrayImage strip_;
    int scale_, glyphWidth_, glyphHeight_;
};

// The font only has ASCII: accented Latin letters lose the accent, other characters become '?' and
// control characters spaces.
std::string foldToAscii(std::string_view utf8);

struct FontChoice {
    size_t index;
    int scale;
    bool operator==(const FontChoice&) const = default;
};

// Picks the biggest font and integer scale whose cell fits about 48 rows on the short side, or
// 15 dp of the display's density when that is bigger; the smallest font when none fits.
FontChoice chooseFont(int width, int height, float density, const std::vector<int>& cellHeights);

struct Layout {
    Rect header, rule, log, logo, bar;
    int logRows, logCols;
};

Layout makeLayout(int width, int height, int cellWidth, int cellHeight);

// Colors taken from the logo: night-blue background, purple and orange accents.
struct Palette {
    Rgb background, title, clock, rule, barTrack, barFill;
    Rgb kinds[6];
    Rgb forKind(LineKind kind) const { return kinds[static_cast<size_t>(kind)]; }
};

const Palette& defaultPalette();

struct FrameText {
    std::string title;
    std::string clock;
};

// The whole frame: header with the title and the clock, the newest log lines that fit, the logo in
// the bottom-right corner and the progress bar (0..100).
void drawFrame(Canvas& canvas, const Layout& layout, const Font& font, const std::vector<Line>& lines,
               const FrameText& text, const Image& logo, int progress);

}  // namespace pc::bootlog

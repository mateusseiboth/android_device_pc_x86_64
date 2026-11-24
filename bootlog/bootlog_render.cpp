/*
 * SPDX-FileCopyrightText: 2026 Mateus Seiboth
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bootlog_render.h"

#include <algorithm>
#include <cmath>

namespace pc::bootlog {

namespace {

constexpr int kGlyphCount = 96;  // 0x20..0x7f
constexpr char kFirstGlyph = 0x20;
constexpr int kMaxScale = 8;
constexpr double kRowsOnShortSide = 48.0;
constexpr float kTextDp = 15.f;
constexpr int kLogoFraction = 5;  // of the short side

// U+00C0..U+00FF (UTF-8 C3 80..C3 BF) without their accents.
constexpr std::string_view kLatin1Letters = "AAAAAA?CEEEEIIIIDNOOOOOxOUUUUY?saaaaaa?ceeeeiiiidnooooo?ouuuuy?y";
static_assert(kLatin1Letters.size() == 64);

uint32_t pack(Rgb c) {
    return 0xff000000u | (uint32_t{c.b} << 16) | (uint32_t{c.g} << 8) | c.r;
}

uint8_t mix(uint8_t dst, uint8_t src, uint8_t alpha) {
    return static_cast<uint8_t>((src * alpha + dst * (255 - alpha)) / 255);
}

size_t sequenceLength(uint8_t lead) {
    if (lead < 0xc0) return 1;
    if (lead < 0xe0) return 2;
    return lead < 0xf0 ? 3 : 4;
}

char foldSequence(std::string_view sequence) {
    const auto lead = static_cast<uint8_t>(sequence[0]);
    if (lead < 0x20 || lead == 0x7f) return ' ';
    if (lead < 0x80) return static_cast<char>(lead);
    if (lead != 0xc3 || sequence.size() != 2) return '?';
    return kLatin1Letters[static_cast<uint8_t>(sequence[1]) & 0x3f];
}

int glyphIndex(char ch) {
    const auto byte = static_cast<uint8_t>(ch);
    const bool printable = byte >= kFirstGlyph && byte < kFirstGlyph + kGlyphCount;
    return (printable ? byte : '?') - kFirstGlyph;
}

struct AreaSum {
    uint64_t alpha = 0, r = 0, g = 0, b = 0;
    int count = 0;

    void add(const uint8_t* px) {
        alpha += px[3];
        r += uint64_t{px[0]} * px[3];
        g += uint64_t{px[1]} * px[3];
        b += uint64_t{px[2]} * px[3];
        ++count;
    }

    void store(uint8_t* out) const {
        const uint64_t a = std::max<uint64_t>(alpha, 1);
        out[0] = static_cast<uint8_t>(r / a);
        out[1] = static_cast<uint8_t>(g / a);
        out[2] = static_cast<uint8_t>(b / a);
        out[3] = static_cast<uint8_t>(alpha / std::max(count, 1));
    }
};

AreaSum sumArea(const Image& source, int x0, int x1, int y0, int y1) {
    AreaSum sum;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) sum.add(&source.rgba[(static_cast<size_t>(y) * source.width + x) * 4]);
    return sum;
}

// [begin, end) of the source pixels that land on destination pixel i (never empty).
std::pair<int, int> sourceSpan(int i, int destination, int source) {
    const int begin = std::min(i * source / destination, source - 1);
    return {begin, std::max(begin + 1, (i + 1) * source / destination)};
}

void drawText(Canvas& canvas, const Font& font, int x, int top, const std::string& text, Rgb color, Weight weight,
              int maxCols) {
    font.draw(canvas, x, top, foldToAscii(text), color, weight, maxCols);
}

void drawHeader(Canvas& canvas, const Layout& layout, const Font& font, const FrameText& text) {
    const Palette& palette = defaultPalette();
    const int pad = font.cellWidth();
    const int top = layout.header.top + (layout.header.height() - font.cellHeight()) / 2;
    const int cols = layout.header.width() / font.cellWidth();
    const std::string clock = foldToAscii(text.clock);
    drawText(canvas, font, layout.header.left + pad, top, text.title, palette.title, Weight::kBold, cols);
    const int clockX = layout.header.right - pad - static_cast<int>(clock.size()) * font.cellWidth();
    drawText(canvas, font, clockX, top, clock, palette.clock, Weight::kRegular, cols);
    canvas.fill(layout.rule, palette.rule);
}

void drawLog(Canvas& canvas, const Layout& layout, const Font& font, const std::vector<Line>& lines) {
    const size_t shown = std::min(lines.size(), static_cast<size_t>(layout.logRows));
    int top = layout.log.top;
    for (auto line = lines.end() - shown; line != lines.end(); ++line, top += font.cellHeight())
        drawText(canvas, font, layout.log.left, top, line->text, defaultPalette().forKind(line->kind),
                 Weight::kRegular, layout.logCols);
}

void drawBar(Canvas& canvas, const Rect& bar, int progress) {
    const Palette& palette = defaultPalette();
    const int filled = bar.width() * std::clamp(progress, 0, 100) / 100;
    canvas.fill(bar, palette.barTrack);
    canvas.fill({bar.left, bar.top, bar.left + filled, bar.bottom}, palette.barFill);
}

}  // namespace

void Canvas::fill(Rect rect, Rgb color) {
    const int x0 = std::max(rect.left, 0), y0 = std::max(rect.top, 0);
    const int x1 = std::min(rect.right, width_), y1 = std::min(rect.bottom, height_);
    const uint32_t value = pack(color);
    for (int y = y0; y < y1; ++y) std::fill(pixels_ + y * stride_ + x0, pixels_ + y * stride_ + std::max(x0, x1), value);
}

void Canvas::blend(int x, int y, Rgb color, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || alpha == 0) return;
    const Rgb dst = at(x, y);
    pixels_[y * stride_ + x] = pack({mix(dst.r, color.r, alpha), mix(dst.g, color.g, alpha), mix(dst.b, color.b, alpha)});
}

Rgb Canvas::at(int x, int y) const {
    const uint32_t p = pixels_[y * stride_ + x];
    return {static_cast<uint8_t>(p), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p >> 16)};
}

Image scaleToFit(const Image& source, int size) {
    if (source.width <= 0 || source.height <= 0 || size <= 0) return {};
    const double factor = std::min(1.0, std::min(static_cast<double>(size) / source.width,
                                                 static_cast<double>(size) / source.height));
    Image out;
    out.width = std::max(1, static_cast<int>(std::lround(source.width * factor)));
    out.height = std::max(1, static_cast<int>(std::lround(source.height * factor)));
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        const auto [y0, y1] = sourceSpan(y, out.height, source.height);
        for (int x = 0; x < out.width; ++x) {
            const auto [x0, x1] = sourceSpan(x, out.width, source.width);
            sumArea(source, x0, x1, y0, y1).store(&out.rgba[(static_cast<size_t>(y) * out.width + x) * 4]);
        }
    }
    return out;
}

void drawImage(Canvas& canvas, const Image& image, int x0, int y0) {
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const uint8_t* px = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
            canvas.blend(x0 + x, y0 + y, {px[0], px[1], px[2]}, px[3]);
        }
}

std::optional<Font> Font::fromStrip(GrayImage strip, int scale) {
    const bool layoutOk = strip.width > 0 && strip.width % kGlyphCount == 0 && strip.height > 0 && strip.height % 2 == 0;
    const bool sizeOk = strip.gray.size() == static_cast<size_t>(strip.width) * strip.height;
    if (!layoutOk || !sizeOk || scale < 1) return std::nullopt;
    return Font(std::move(strip), scale);
}

Font::Font(GrayImage strip, int scale)
    : strip_(std::move(strip)), scale_(scale), glyphWidth_(strip_.width / kGlyphCount), glyphHeight_(strip_.height / 2) {}

void Font::draw(Canvas& canvas, int x, int top, std::string_view text, Rgb color, Weight weight, int maxCols) const {
    const int row = weight == Weight::kBold ? 1 : 0;
    const size_t count = std::min(text.size(), static_cast<size_t>(std::max(maxCols, 0)));
    for (size_t i = 0; i < count && x < canvas.width(); ++i, x += cellWidth())
        drawGlyph(canvas, x, top, glyphIndex(text[i]), row, color);
}

void Font::drawGlyph(Canvas& canvas, int x, int top, int glyph, int row, Rgb color) const {
    for (int gy = 0; gy < glyphHeight_; ++gy) {
        const uint8_t* coverage = &strip_.gray[static_cast<size_t>(row * glyphHeight_ + gy) * strip_.width + glyph * glyphWidth_];
        for (int gx = 0; gx < glyphWidth_; ++gx) {
            if (coverage[gx] == 0) continue;
            for (int sy = 0; sy < scale_; ++sy)
                for (int sx = 0; sx < scale_; ++sx)
                    canvas.blend(x + gx * scale_ + sx, top + gy * scale_ + sy, color, coverage[gx]);
        }
    }
}

std::string foldToAscii(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const size_t length = std::min(sequenceLength(static_cast<uint8_t>(utf8[i])), utf8.size() - i);
        out.push_back(foldSequence(utf8.substr(i, length)));
        i += length;
    }
    return out;
}

FontChoice chooseFont(int width, int height, float density, const std::vector<int>& cellHeights) {
    const double target = std::max(std::min(width, height) / kRowsOnShortSide, static_cast<double>(kTextDp * density));
    const auto smallest = std::min_element(cellHeights.begin(), cellHeights.end());
    FontChoice best{static_cast<size_t>(smallest - cellHeights.begin()), 1};
    int bestCell = 0;
    for (size_t index = 0; index < cellHeights.size(); ++index)
        for (int scale = 1; scale <= kMaxScale; ++scale) {
            const int cell = cellHeights[index] * scale;
            if (cell > target || cell <= bestCell) continue;
            best = {index, scale};
            bestCell = cell;
        }
    return best;
}

Layout makeLayout(int width, int height, int cellWidth, int cellHeight) {
    const int pad = cellWidth;
    Layout layout{};
    layout.header = {0, 0, width, 2 * cellHeight};
    layout.rule = {0, layout.header.bottom, width, layout.header.bottom + std::max(1, cellHeight / 16)};
    const int barTop = height - cellHeight;
    layout.bar = {pad, barTop, width - pad, barTop + std::max(2, cellHeight / 2)};
    layout.log = {pad, layout.rule.bottom + cellHeight / 2, width - pad, barTop - cellHeight / 2};
    layout.logRows = std::max(1, layout.log.height() / cellHeight);
    layout.logCols = std::max(1, layout.log.width() / cellWidth);
    const int logo = std::min(std::min(width, height) / kLogoFraction, layout.log.height());
    layout.logo = {layout.log.right - logo, layout.log.bottom - logo, layout.log.right, layout.log.bottom};
    return layout;
}

const Palette& defaultPalette() {
    static const Palette palette = {
            .background = {11, 3, 27},
            .title = {176, 148, 255},
            .clock = {245, 152, 54},
            .rule = {48, 23, 130},
            .barTrack = {28, 11, 81},
            .barFill = {245, 152, 54},
            // Indexed by LineKind: normal, error, warning, kernel, progress, runtime.
            .kinds = {{170, 170, 185}, {255, 95, 110}, {255, 200, 90}, {110, 220, 140}, {176, 148, 255}, {120, 190, 255}},
    };
    return palette;
}

void drawFrame(Canvas& canvas, const Layout& layout, const Font& font, const std::vector<Line>& lines,
               const FrameText& text, const Image& logo, int progress) {
    canvas.fill({0, 0, canvas.width(), canvas.height()}, defaultPalette().background);
    drawHeader(canvas, layout, font, text);
    drawLog(canvas, layout, font, lines);
    drawImage(canvas, logo, layout.logo.right - logo.width, layout.logo.bottom - logo.height);
    drawBar(canvas, layout.bar, progress);
}

}  // namespace pc::bootlog

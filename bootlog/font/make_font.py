#!/usr/bin/env python3
"""Rasterizes a monospace TrueType font into the boot log's glyph strip.

Usage: make_font.py <font.ttf> <cell height> <out dir>   ->   <out dir>/font-<w>x<h>.png

The strip is an 8-bit gray PNG: every character of CHARSET (ASCII 0x20..0x7e, then Latin-1
0xa0..0xff, the order glyphIndex() in bootlog_render.cpp expects) side by side in cells of w x h,
regular on the top row and bold (the regular glyph drawn twice, one pixel apart) on the bottom one.
The committed strips come from frameworks/base/data/fonts/DroidSansMono.ttf at heights 22 and 32:

    for h in 22 32; do python3 make_font.py $ANDROID_BUILD_TOP/frameworks/base/data/fonts/DroidSansMono.ttf $h .; done
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CHARSET = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))


def fit_font(path, cell_height):
    """The biggest pixel size whose ascent plus descent fits the cell."""
    size = cell_height
    while size > 1 and sum(ImageFont.truetype(path, size).getmetrics()) > cell_height:
        size -= 1
    return ImageFont.truetype(path, size)


def draw_cell(font, code, width, height, bold):
    cell = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(cell)
    ascent, descent = font.getmetrics()
    baseline = (height - ascent - descent) // 2 + ascent
    for dx in (0, 1) if bold else (0,):
        draw.text((dx, baseline), chr(code), font=font, fill=255, anchor="ls")
    return cell


def make_strip(path, cell_height):
    font = fit_font(path, cell_height)
    width = math.ceil(font.getlength("M")) + 1  # room for the bold copy
    strip = Image.new("L", (width * len(CHARSET), 2 * cell_height), 0)
    for index, code in enumerate(CHARSET):
        for row, bold in enumerate((False, True)):
            strip.paste(draw_cell(font, code, width, cell_height, bold), (index * width, row * cell_height))
    return strip


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__)
    path, cell_height, out = argv[1], int(argv[2]), Path(argv[3])
    strip = make_strip(path, cell_height)
    strip.save(out / f"font-{strip.width // len(CHARSET)}x{cell_height}.png", optimize=True)


if __name__ == "__main__":
    main(sys.argv)

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
FONT_PATH = ROOT / "assets" / "fonts" / "rajdhani" / "Rajdhani-Bold.ttf"
OUTPUT_HEADER = ROOT / "include" / "display" / "ClockDigitFont.h"
OUTPUT_SOURCE = ROOT / "src" / "display" / "ClockDigitFont.cpp"
PREVIEW_PATH = ROOT / "tools" / "fontgen" / "clock-digit-font-preview.png"
CHARS = "0123456789:"
ALPHA_BITS = 4

SETS = [
    ("Main", 84, 2),
    ("Secondary", 26, 1),
]
def pack_bitmap(mask: Image.Image, bits_per_pixel: int) -> list[int]:
    data = list(mask.getdata())
    packed: list[int] = []
    if bits_per_pixel == 1:
        for index in range(0, len(data), 8):
            value = 0
            for bit in range(8):
                pixel_index = index + bit
                if pixel_index < len(data) and data[pixel_index] >= 128:
                    value |= 1 << (7 - bit)
            packed.append(value)
        return packed

    if bits_per_pixel == 2:
        for index in range(0, len(data), 4):
            value = 0
            for shift_index in range(4):
                pixel_index = index + shift_index
                quantized = 0
                if pixel_index < len(data):
                    quantized = min(3, int(round((data[pixel_index] / 255.0) * 3.0)))
                value |= quantized << (6 - (shift_index * 2))
            packed.append(value)
        return packed

    if bits_per_pixel == 4:
        for index in range(0, len(data), 2):
            value = 0
            for shift_index in range(2):
                pixel_index = index + shift_index
                quantized = 0
                if pixel_index < len(data):
                    quantized = min(15, int(round((data[pixel_index] / 255.0) * 15.0)))
                value |= quantized << (4 - (shift_index * 4))
            packed.append(value)
        return packed

    raise ValueError(f"Unsupported bits_per_pixel: {bits_per_pixel}")
    return packed


def glyph_data(font: ImageFont.FreeTypeFont, ch: str, common_top: int):
    scratch = Image.new("L", (256, 256), 0)
    draw = ImageDraw.Draw(scratch)
    bbox = draw.textbbox((0, 0), ch, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-bbox[0], -bbox[1]), ch, font=font, fill=255)
    packed = pack_bitmap(image, ALPHA_BITS)
    advance = int(round(font.getlength(ch)))
    return {
        "char": ch,
        "width": width,
        "height": height,
        "y_offset": bbox[1] - common_top,
        "advance": advance,
        "packed": packed,
        "mask": image,
    }


def build_sets():
    result = []
    for name, font_size, tracking in SETS:
        font = ImageFont.truetype(str(FONT_PATH), font_size)
        scratch = Image.new("L", (256, 256), 0)
        draw = ImageDraw.Draw(scratch)
        boxes = {ch: draw.textbbox((0, 0), ch, font=font) for ch in CHARS}
        common_top = min(box[1] for box in boxes.values())
        common_bottom = max(box[3] for box in boxes.values())
        glyphs = [glyph_data(font, ch, common_top) for ch in CHARS]
        result.append(
            {
                "name": name,
                "tracking": tracking,
                "line_height": common_bottom - common_top,
                "glyphs": glyphs,
            }
        )
    return result


def write_header():
    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HEADER.write_text(
        """// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ClockDigitFont {

enum class Kind : uint8_t {
    Main = 0,
    Secondary = 1,
};

struct Glyph {
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    int8_t yOffset;
    const uint8_t* bitmap;
};

struct FontSet {
    uint8_t lineHeight;
    uint8_t tracking;
    uint8_t bitsPerPixel;
};

auto glyph(Kind kind, char value) -> const Glyph*;
auto fontSet(Kind kind) -> const FontSet&;

}  // namespace ClockDigitFont
""",
        encoding="utf-8",
    )


def cpp_array(values: list[int]) -> str:
    if not values:
        return ""
    lines = []
    for start in range(0, len(values), 16):
        chunk = values[start : start + 16]
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk))
    return ",\n".join(lines)


def write_source(font_sets):
    OUTPUT_SOURCE.parent.mkdir(parents=True, exist_ok=True)
    parts: list[str] = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "#include <pgmspace.h>",
        '#include "display/ClockDigitFont.h"',
        "",
        "namespace ClockDigitFont {",
        "",
    ]

    for font_set in font_sets:
        prefix = font_set["name"].upper()
        for glyph in font_set["glyphs"]:
            name = glyph["char"]
            array_name = f"{prefix}_{ord(name)}_DATA"
            glyph_name = f"{prefix}_{ord(name)}_GLYPH"
            parts.append(f"static const uint8_t {array_name}[] PROGMEM = {{")
            parts.append(cpp_array(glyph["packed"]))
            parts.append("};")
            parts.append(
                f"static const Glyph {glyph_name} = "
                f"{{{glyph['width']}, {glyph['height']}, {glyph['advance']}, {glyph['y_offset']}, {array_name}}};"
            )
        parts.append(
            f"static const FontSet {prefix}_FONT = "
            f"{{{font_set['line_height']}, {font_set['tracking']}, {ALPHA_BITS}}};"
        )
        parts.append("")

    parts.extend(
        [
            "auto glyph(Kind kind, char value) -> const Glyph* {",
            "    switch (kind) {",
            "        case Kind::Main:",
            "            switch (value) {",
        ]
    )
    for glyph in font_sets[0]["glyphs"]:
        parts.append(f"                case '{glyph['char']}': return &MAIN_{ord(glyph['char'])}_GLYPH;")
    parts.extend(
        [
            "                default: return nullptr;",
            "            }",
            "        case Kind::Secondary:",
            "            switch (value) {",
        ]
    )
    for glyph in font_sets[1]["glyphs"]:
        parts.append(f"                case '{glyph['char']}': return &SECONDARY_{ord(glyph['char'])}_GLYPH;")
    parts.extend(
        [
            "                default: return nullptr;",
            "            }",
            "        default:",
            "            return nullptr;",
            "    }",
            "}",
            "",
            "auto fontSet(Kind kind) -> const FontSet& {",
            "    switch (kind) {",
            "        case Kind::Secondary:",
            "            return SECONDARY_FONT;",
            "        case Kind::Main:",
            "        default:",
            "            return MAIN_FONT;",
            "    }",
            "}",
            "",
            "}  // namespace ClockDigitFont",
            "",
        ]
    )
    OUTPUT_SOURCE.write_text("\n".join(parts), encoding="utf-8")


def write_preview(font_sets):
    canvas = Image.new("RGB", (360, 160), (8, 8, 12))
    draw = ImageDraw.Draw(canvas)
    y = 10
    for font_set in font_sets:
        draw.text((10, y), font_set["name"], fill=(180, 180, 180))
        y += 18
        x = 10
        for glyph in font_set["glyphs"]:
            alpha = glyph["mask"]
            color = Image.new("RGB", alpha.size, (255, 255, 255))
            canvas.paste(color, (x, y + glyph["y_offset"]), alpha)
            x += glyph["advance"] + font_set["tracking"]
        y += font_set["line_height"] + 18
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(PREVIEW_PATH)


def main():
    if not FONT_PATH.exists():
        raise SystemExit(f"Font not found: {FONT_PATH}")

    font_sets = build_sets()
    write_header()
    write_source(font_sets)
    write_preview(font_sets)
    print(OUTPUT_HEADER)
    print(OUTPUT_SOURCE)
    print(PREVIEW_PATH)


if __name__ == "__main__":
    main()

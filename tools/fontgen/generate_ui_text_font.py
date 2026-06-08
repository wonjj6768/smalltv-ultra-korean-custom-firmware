from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_HEADER = ROOT / "include" / "display" / "UiTextFont.h"
OUTPUT_SOURCE = ROOT / "src" / "display" / "UiTextFont.cpp"
PREVIEW_PATH = ROOT / "tools" / "fontgen" / "ui-text-font-preview.png"
EXTRA_CHAR_FILES = [
    ROOT / "tools" / "fontgen" / "korean-region-chars.txt",
]

FONT_CANDIDATES = [
    ROOT / "tools" / "fontgen" / "assets" / "fonts" / "noto_sans_kr" / "NotoSansKR-VF.ttf",
    Path("C:/Windows/Fonts/NotoSansKR-VF.ttf"),
]

ASCII_CHARS = " 0123456789/:.%+-℃"
UI_STRINGS = [
    "와이파이 연결 중...",
    "연결되었습니다!",
    "연결에 실패했습니다!",
    "업로드 중...",
    "업데이트있음",
    "취소됨",
    "완료!",
    "중단됨",
    "일",
    "월",
    "화",
    "수",
    "목",
    "금",
    "토",
    "시",
    "오전",
    "오후",
    "서울",
    "도쿄",
    "상하이",
    "싱가포르",
    "방콕",
    "런던",
    "베를린",
    "뉴욕",
    "시카고",
    "덴버",
    "로스앤젤레스",
    "시드니",
    "시간",
    "시간 동기화 대기 중",
    "와이파이 연결 후 NTP 동기화",
    "SmallTV-Ultra Korean Custom Firmware",
    "오존",
    "최고기온",
    "PM",
    "IP",
    "km",
    "mm",
    "℃",
]

SETS = [
    ("Small", 14, 1),
    ("Large", 20, 1),
]
ALPHA_BITS = 4


def load_font_path() -> Path:
    for candidate in FONT_CANDIDATES:
        if candidate.exists():
            return candidate
    raise SystemExit("Noto Sans KR not found for UI text generation")


def collect_chars() -> str:
    chars = []
    for ch in ASCII_CHARS:
        if ch not in chars:
            chars.append(ch)
    for sample in UI_STRINGS:
        for ch in sample:
            if ch not in chars:
                chars.append(ch)
    for extra_char_file in EXTRA_CHAR_FILES:
        if not extra_char_file.exists():
            continue
        for ch in extra_char_file.read_text(encoding="utf-8"):
            if ch.isspace():
                continue
            if ch not in chars:
                chars.append(ch)
    return "".join(chars)


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


def prepare_font(font_path: Path, font_size: int) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(font_path), font_size)
    if hasattr(font, "set_variation_by_name"):
        try:
            font.set_variation_by_name("Bold")
        except OSError:
            pass
    if hasattr(font, "set_variation_by_axes"):
        try:
            font.set_variation_by_axes([700])
        except OSError:
            pass
    return font


def glyph_data(font: ImageFont.FreeTypeFont, ch: str, common_top: int) -> dict[str, object]:
    scratch = Image.new("L", (256, 256), 0)
    draw = ImageDraw.Draw(scratch)
    bbox = draw.textbbox((0, 0), ch, font=font)
    width = max(0, bbox[2] - bbox[0])
    height = max(0, bbox[3] - bbox[1])
    image = Image.new("L", (max(1, width), max(1, height)), 0)
    if width > 0 and height > 0:
        draw = ImageDraw.Draw(image)
        draw.text((-bbox[0], -bbox[1]), ch, font=font, fill=255)
    packed = pack_bitmap(image.crop((0, 0, width, height)), ALPHA_BITS) if width > 0 and height > 0 else []
    advance = int(round(font.getlength(ch)))
    return {
        "char": ch,
        "codepoint": ord(ch),
        "width": width,
        "height": height,
        "y_offset": bbox[1] - common_top,
        "advance": advance,
        "packed": packed,
        "mask": image.crop((0, 0, width, height)) if width > 0 and height > 0 else Image.new("L", (1, 1), 0),
    }


def build_sets(chars: str, font_path: Path) -> list[dict[str, object]]:
    result = []
    for name, font_size, tracking in SETS:
        font = prepare_font(font_path, font_size)
        scratch = Image.new("L", (256, 256), 0)
        draw = ImageDraw.Draw(scratch)
        boxes = {ch: draw.textbbox((0, 0), ch, font=font) for ch in chars}
        common_top = min(box[1] for box in boxes.values())
        common_bottom = max(box[3] for box in boxes.values())
        glyphs = [glyph_data(font, ch, common_top) for ch in chars]
        result.append(
            {
                "name": name,
                "tracking": tracking,
                "line_height": common_bottom - common_top,
                "glyphs": glyphs,
            }
        )
    return result


def cpp_array(values: list[int]) -> str:
    if not values:
        return ""
    lines = []
    for start in range(0, len(values), 16):
        chunk = values[start : start + 16]
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk))
    return ",\n".join(lines)


def write_header() -> None:
    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HEADER.write_text(
        """// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace UiTextFont {

enum class Kind : uint8_t {
    Small = 0,
    Large = 1,
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

auto glyph(Kind kind, uint32_t codepoint) -> const Glyph*;
auto fontSet(Kind kind) -> const FontSet&;

}  // namespace UiTextFont
""",
        encoding="utf-8",
    )


def write_source(font_sets: list[dict[str, object]]) -> None:
    OUTPUT_SOURCE.parent.mkdir(parents=True, exist_ok=True)
    parts: list[str] = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "#include <pgmspace.h>",
        '#include "display/UiTextFont.h"',
        "",
        "namespace UiTextFont {",
        "",
        "struct GlyphEntry {",
        "    uint32_t codepoint;",
        "    Glyph glyph;",
        "};",
        "",
    ]

    for font_set in font_sets:
        prefix = str(font_set["name"]).upper()
        for glyph in font_set["glyphs"]:
            codepoint = int(glyph["codepoint"])
            tag = f"U{codepoint:04X}"
            array_name = f"{prefix}_{tag}_DATA"
            parts.append(f"static const uint8_t {array_name}[] PROGMEM = {{")
            parts.append(cpp_array(glyph["packed"]))
            parts.append("};")
        parts.append(
            f"static const FontSet {prefix}_FONT = "
            f"{{{font_set['line_height']}, {font_set['tracking']}, {ALPHA_BITS}}};"
        )
        parts.append(f"static const GlyphEntry {prefix}_GLYPHS[] PROGMEM = {{")
        for glyph in font_set["glyphs"]:
            codepoint = int(glyph["codepoint"])
            tag = f"U{codepoint:04X}"
            array_name = f"{prefix}_{tag}_DATA"
            parts.append(
                "    "
                f"{{0x{codepoint:04X}u, "
                f"{{{glyph['width']}, {glyph['height']}, {glyph['advance']}, {glyph['y_offset']}, {array_name}}}}},"
            )
        parts.append("};")
        parts.append("")

    parts.extend(
        [
            "static auto findGlyph(const GlyphEntry* entries, uint16_t count, uint32_t codepoint) -> const Glyph* {",
            "    static Glyph match{};",
            "    for (uint16_t index = 0; index < count; ++index) {",
            "        GlyphEntry entry{};",
            "        memcpy_P(&entry, &entries[index], sizeof(GlyphEntry));",
            "        if (entry.codepoint == codepoint) {",
            "            match = entry.glyph;",
            "            return &match;",
            "        }",
            "    }",
            "    return nullptr;",
            "}",
            "",
        ]
    )

    parts.extend(
        [
            "auto glyph(Kind kind, uint32_t codepoint) -> const Glyph* {",
            "    switch (kind) {",
            "        case Kind::Large:",
            "            return findGlyph(LARGE_GLYPHS, sizeof(LARGE_GLYPHS) / sizeof(LARGE_GLYPHS[0]), codepoint);",
            "        case Kind::Small:",
            "        default:",
            "            return findGlyph(SMALL_GLYPHS, sizeof(SMALL_GLYPHS) / sizeof(SMALL_GLYPHS[0]), codepoint);",
            "    }",
            "}",
            "",
            "auto fontSet(Kind kind) -> const FontSet& {",
            "    switch (kind) {",
            "        case Kind::Large:",
            "            return LARGE_FONT;",
            "        case Kind::Small:",
            "        default:",
            "            return SMALL_FONT;",
            "    }",
            "}",
            "",
            "}  // namespace UiTextFont",
            "",
        ]
    )

    OUTPUT_SOURCE.write_text("\n".join(parts), encoding="utf-8")


def write_preview(font_sets: list[dict[str, object]], chars: str) -> None:
    sample = "2026/04/08 수 19시 20시 21시 22시"
    canvas = Image.new("RGB", (900, 140), (8, 8, 12))
    draw = ImageDraw.Draw(canvas)
    y = 10
    for font_set in font_sets:
        draw.text((12, y), str(font_set["name"]), fill=(180, 180, 180))
        y += 20
        x = 12
        for ch in sample:
            glyph = next(g for g in font_set["glyphs"] if g["char"] == ch)
            alpha = glyph["mask"]
            if glyph["width"] > 0 and glyph["height"] > 0:
                color = Image.new("RGB", alpha.size, (255, 255, 255))
                canvas.paste(color, (x, y + glyph["y_offset"]), alpha)
            x += glyph["advance"] + int(font_set["tracking"])
        y += int(font_set["line_height"]) + 22
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(PREVIEW_PATH)


def main() -> None:
    font_path = load_font_path()
    chars = collect_chars()
    font_sets = build_sets(chars, font_path)
    write_header()
    write_source(font_sets)
    write_preview(font_sets, chars)
    print(font_path)
    print(OUTPUT_HEADER)
    print(OUTPUT_SOURCE)
    print(PREVIEW_PATH)


if __name__ == "__main__":
    main()

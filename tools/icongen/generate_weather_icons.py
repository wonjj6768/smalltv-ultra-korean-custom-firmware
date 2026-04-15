from __future__ import annotations

import urllib.request
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
CACHE_DIR = ROOT / "tools" / "display-emulator" / "meteocons-cache"
OUTPUT_DIR = ROOT / "data" / "weather-icons"

ICON_MAP = {
    "clear": "clear-day",
    "cloudy": "cloudy",
    "rain": "overcast-rain",
    "snow": "snow",
    "storm": "thunderstorms",
    "fog": "fog",
    "umbrella": "umbrella",
}

ICON_SIZES = {
    "clear": (52, 28),
    "cloudy": (52, 28),
    "rain": (52, 28),
    "snow": (52, 28),
    "storm": (52, 28),
    "fog": (52, 28),
    "umbrella": (16,),
}


def ensure_png(name: str) -> Path:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    png_path = CACHE_DIR / f"{name}.png"
    if png_path.exists():
        return png_path

    url = f"https://raw.githubusercontent.com/basmilius/weather-icons/dev/production/fill/png/128/{name}.png"
    urllib.request.urlretrieve(url, png_path)
    return png_path


def render_bmp(src: Path, dst: Path) -> None:
    image = Image.open(src).convert("RGBA")
    bbox = image.getbbox()
    if bbox:
        image = image.crop(bbox)

    background = Image.new("RGB", image.size, (0, 0, 0))
    background.paste(image, mask=image.getchannel("A"))
    background.save(dst, format="BMP")


def render_contained_bmp(src: Path, dst: Path, size: int) -> None:
    image = Image.open(src).convert("RGBA")
    bbox = image.getbbox()
    if bbox:
        image = image.crop(bbox)

    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    image.thumbnail((size, size), Image.Resampling.LANCZOS)
    offset = ((size - image.width) // 2, (size - image.height) // 2)
    canvas.alpha_composite(image, offset)

    background = Image.new("RGB", canvas.size, (0, 0, 0))
    background.paste(canvas, mask=canvas.getchannel("A"))
    background.save(dst, format="BMP")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for slot, name in ICON_MAP.items():
        png_path = ensure_png(name)
        bmp_path = OUTPUT_DIR / f"{slot}.bmp"
        render_bmp(png_path, bmp_path)
        print(f"{slot}: {bmp_path}")
        for size in ICON_SIZES.get(slot, ()):
            sized_path = OUTPUT_DIR / f"{slot}-{size}.bmp"
            render_contained_bmp(png_path, sized_path, size)
            print(f"{slot}@{size}: {sized_path}")


if __name__ == "__main__":
    main()

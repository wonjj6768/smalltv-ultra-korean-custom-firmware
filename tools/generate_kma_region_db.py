from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import openpyxl


def normalize_level(value: object) -> str:
    return str(value or "").strip()


def compact_sigungu(value: str) -> str:
    return re.sub(r"(.+?시)(.+구)$", r"\1 \2", value)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate_kma_region_db.py <kma-xlsx> <output-json>")
        return 1

    xlsx_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    workbook = openpyxl.load_workbook(xlsx_path, read_only=True, data_only=True)
    worksheet = workbook.active
    regions = []
    seen = set()

    for row in worksheet.iter_rows(min_row=2, values_only=True):
        if normalize_level(row[0]) != "kor":
            continue

        level1 = normalize_level(row[2])
        level2 = compact_sigungu(normalize_level(row[3]))
        level3 = normalize_level(row[4])
        if level3:
            continue
        if level2 == level1:
            level2 = ""

        label = level2 or level1
        if not label:
            continue
        if float(row[13]) == 0.0 or float(row[14]) == 0.0:
            continue

        full = f"{level1} {label}".strip() if level2 else level1
        key = (full, int(row[5]), int(row[6]))
        if key in seen:
            continue
        seen.add(key)

        regions.append(
            {
                "label": label,
                "full": full,
                "x": int(row[5]),
                "y": int(row[6]),
                "lat": round(float(row[14]), 6),
                "lon": round(float(row[13]), 6),
            }
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(regions, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8",
    )
    print(f"wrote {len(regions)} regions to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

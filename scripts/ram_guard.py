from __future__ import annotations

import os
import subprocess
from pathlib import Path

Import("env")


RAM_LIMIT_BYTES = int(env.GetProjectOption("custom_ram_limit", "52000"))


def _size_tool_path() -> Path:
    toolchain_dir = env.PioPlatform().get_package_dir("toolchain-xtensa")
    if not toolchain_dir:
        raise RuntimeError("toolchain-xtensa package dir not found")
    tool_name = "xtensa-lx106-elf-size.exe" if os.name == "nt" else "xtensa-lx106-elf-size"
    return Path(toolchain_dir) / "bin" / tool_name


def _parse_size_totals(elf_path: str) -> tuple[int, int]:
    size_tool = _size_tool_path()
    result = subprocess.run(
        [str(size_tool), "-A", "-d", elf_path],
        capture_output=True,
        text=True,
        check=True,
    )

    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if len(lines) < 2:
        raise RuntimeError(f"Unexpected size output:\n{result.stdout}")
    sections: dict[str, int] = {}
    for line in lines[1:]:
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[0]
        if name == "Total":
            continue
        try:
            sections[name] = int(parts[1])
        except ValueError:
            continue

    flash_bytes = sections.get(".irom0.text", 0) + sections.get(".text1", 0) + sections.get(".text", 0)
    ram_bytes = (
        sections.get(".data", 0)
        + sections.get(".rodata", 0)
        + sections.get(".bss", 0)
        + sections.get(".noinit", 0)
    )
    return flash_bytes, ram_bytes


def _check_ram_limit(source, target, env) -> None:
    elf_path = str(target[0])
    _flash_bytes, ram_bytes = _parse_size_totals(elf_path)
    print(f"[ram_guard] RAM usage {ram_bytes} bytes / limit {RAM_LIMIT_BYTES} bytes")
    if ram_bytes > RAM_LIMIT_BYTES:
        raise RuntimeError(
            f"RAM usage {ram_bytes} bytes exceeds safety limit {RAM_LIMIT_BYTES} bytes. "
            "Trim runtime memory before shipping this build."
        )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _check_ram_limit)

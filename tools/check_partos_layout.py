#!/usr/bin/env python3
#
# check_partos_layout.py
#
# Fast contract check for the split PartOS images. This catches the class of
# regressions where sizes drift past their reserved windows or a moved symbol
# silently breaks emulator/runtime code that resolves fixed locations from the
# linker maps.
#
# 2026-06-28   tstih

from __future__ import annotations

import argparse
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
KERNEL_MAP = ROOT / "partos" / "build" / "kernel.map"
OS_MAP = ROOT / "partos" / "build" / "os.map"
KERNEL_SYS = ROOT / "partos" / "bin" / "kernel.sys"
OS_SYS = ROOT / "partos" / "bin" / "os.sys"

SECTOR_SIZE = 256
UKERNEL_SECTORS = 16
SERVICES_SECTORS = 64
KERNEL_STACK_TOP = 0xFE00


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_file(path: pathlib.Path) -> pathlib.Path:
    if not path.is_file():
        fail(f"missing required build artifact: {path}")
    return path


def find_area_addr(path: pathlib.Path, area: str) -> int:
    pattern = re.compile(rf"^{re.escape(area)}\s+([0-9A-Fa-f]{{8}})\s+", re.MULTILINE)
    text = require_file(path).read_text(encoding="ascii", errors="replace")
    match = pattern.search(text)
    if not match:
        fail(f"area {area} not found in {path}")
    return int(match.group(1), 16)


def find_area_size(path: pathlib.Path, area: str) -> int:
    pattern = re.compile(
        rf"^{re.escape(area)}\s+[0-9A-Fa-f]{{8}}\s+([0-9A-Fa-f]{{8}})\s+=",
        re.MULTILINE,
    )
    text = require_file(path).read_text(encoding="ascii", errors="replace")
    match = pattern.search(text)
    if not match:
        fail(f"area size for {area} not found in {path}")
    return int(match.group(1), 16)


def find_symbol_addr(path: pathlib.Path, symbol: str) -> int:
    text = require_file(path).read_text(encoding="ascii", errors="replace")
    for line in text.splitlines():
        if symbol not in line:
            continue
        match = re.match(r"\s*([0-9A-Fa-f]{8})\s+", line)
        if match:
            return int(match.group(1), 16)
    fail(f"symbol {symbol} not found in {path}")


def check_equal(label: str, actual: int, expected: int) -> None:
    if actual != expected:
        fail(f"{label} is 0x{actual:04X}, expected 0x{expected:04X}")
    print(f"ok: {label} = 0x{actual:04X}")


def check_le(label: str, actual: int, limit: int) -> None:
    if actual > limit:
        fail(f"{label} is {actual} bytes, limit is {limit} bytes")
    print(f"ok: {label} = {actual} bytes (limit {limit})")


def check_range_fit(label: str, start: int, size: int, limit: int) -> None:
    end = start + size
    if end > limit:
        fail(
            f"{label} spans 0x{start:04X}..0x{end - 1:04X}, "
            f"which crosses 0x{limit:04X}"
        )
    print(f"ok: {label} spans 0x{start:04X}..0x{end - 1:04X}")


def collect_layout() -> dict[str, int]:
    kernel_size = require_file(KERNEL_SYS).stat().st_size
    os_size = require_file(OS_SYS).stat().st_size

    layout = {
        "kernel_code_base": find_area_addr(KERNEL_MAP, "_CODE"),
        "os_code_base": find_area_addr(OS_MAP, "_CODE"),
        "kernel_size": kernel_size,
        "os_size": os_size,
        "kernel_sysvars_base": find_area_addr(KERNEL_MAP, "_SYSVARS"),
        "kernel_sysvars_size": find_area_size(KERNEL_MAP, "_SYSVARS"),
        "kernel_initialized_base": find_area_addr(KERNEL_MAP, "_INITIALIZED"),
        "kernel_initialized_size": find_area_size(KERNEL_MAP, "_INITIALIZED"),
        "kernel_heap_base": find_area_addr(KERNEL_MAP, "_HEAP"),
        "kernel_heap_size": find_area_size(KERNEL_MAP, "_HEAP"),
        "hd_dev0": find_symbol_addr(OS_MAP, "hd_dev0"),
        "hd_read": find_symbol_addr(OS_MAP, "hd_read"),
        "hd_dma_setup": find_symbol_addr(OS_MAP, "hd_dma_setup$"),
        "hd_dma_abort": find_symbol_addr(OS_MAP, "hd_dma_abort$"),
        "boot_event": find_symbol_addr(OS_MAP, "boot_event$"),
        "fat_queue_event": find_symbol_addr(OS_MAP, "fat_queue_event$"),
        "fat_io_event": find_symbol_addr(OS_MAP, "fat_io_event$"),
        "sys_nvram_cache": find_symbol_addr(OS_MAP, "__sys_nvram_cache"),
    }
    layout["hd_io_ptr"] = layout["hd_dev0"] + 0x14
    layout["hd_dma_trace_lo"] = layout["hd_dma_setup"]
    layout["hd_dma_trace_hi"] = layout["hd_dma_abort"] - 1
    return layout


def validate_layout(layout: dict[str, int]) -> None:
    check_equal("kernel _CODE base", layout["kernel_code_base"], 0x0000)
    check_equal("os _CODE base", layout["os_code_base"], 0xC000)
    check_le("kernel.sys size", layout["kernel_size"], UKERNEL_SECTORS * SECTOR_SIZE)
    check_le("os.sys size", layout["os_size"], SERVICES_SECTORS * SECTOR_SIZE)
    check_range_fit(
        "kernel _SYSVARS",
        layout["kernel_sysvars_base"],
        layout["kernel_sysvars_size"],
        layout["kernel_initialized_base"],
    )
    check_range_fit(
        "kernel _INITIALIZED",
        layout["kernel_initialized_base"],
        layout["kernel_initialized_size"],
        layout["kernel_heap_base"],
    )
    check_range_fit(
        "kernel _HEAP",
        layout["kernel_heap_base"],
        layout["kernel_heap_size"],
        KERNEL_STACK_TOP,
    )

    if layout["hd_dma_abort"] <= layout["hd_dma_setup"]:
        fail(
            f"hd_dma_abort$ (0x{layout['hd_dma_abort']:04X}) must follow "
            f"hd_dma_setup$ (0x{layout['hd_dma_setup']:04X})"
        )

    print(
        "ok: resolved OS symbols "
        f"hd_dev0=0x{layout['hd_dev0']:04X} hd_read=0x{layout['hd_read']:04X} "
        f"hd_dma_setup=0x{layout['hd_dma_setup']:04X} hd_dma_abort=0x{layout['hd_dma_abort']:04X} "
        f"boot_event=0x{layout['boot_event']:04X} fat_queue_event=0x{layout['fat_queue_event']:04X} "
        f"fat_io_event=0x{layout['fat_io_event']:04X} "
        f"sys_nvram_cache=0x{layout['sys_nvram_cache']:04X}"
    )


def write_header(path: pathlib.Path, layout: dict[str, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = f"""#pragma once

// Generated by tools/check_partos_layout.py. Do not edit by hand.

#include <cstdint>

namespace partos_layout {{
inline constexpr std::uint16_t kernel_code_base = 0x{layout["kernel_code_base"]:04X};
inline constexpr std::uint16_t os_code_base = 0x{layout["os_code_base"]:04X};
inline constexpr std::uint16_t hd_dev0 = 0x{layout["hd_dev0"]:04X};
inline constexpr std::uint16_t hd_io_ptr = 0x{layout["hd_io_ptr"]:04X};
inline constexpr std::uint16_t hd_read = 0x{layout["hd_read"]:04X};
inline constexpr std::uint16_t hd_dma_setup = 0x{layout["hd_dma_setup"]:04X};
inline constexpr std::uint16_t hd_dma_abort = 0x{layout["hd_dma_abort"]:04X};
inline constexpr std::uint16_t hd_dma_trace_lo = 0x{layout["hd_dma_trace_lo"]:04X};
inline constexpr std::uint16_t hd_dma_trace_hi = 0x{layout["hd_dma_trace_hi"]:04X};
inline constexpr std::uint16_t boot_event = 0x{layout["boot_event"]:04X};
inline constexpr std::uint16_t fat_queue_event = 0x{layout["fat_queue_event"]:04X};
inline constexpr std::uint16_t fat_io_event = 0x{layout["fat_io_event"]:04X};
inline constexpr std::uint16_t sys_nvram_cache = 0x{layout["sys_nvram_cache"]:04X};
inline constexpr std::uint32_t kernel_size = {layout["kernel_size"]};
inline constexpr std::uint32_t os_size = {layout["os_size"]};
}}  // namespace partos_layout
"""
    if path.exists() and path.read_text(encoding="ascii") == header:
        return
    path.write_text(header, encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--header",
        type=pathlib.Path,
        help="Optional output path for a generated C++ layout header.",
    )
    args = parser.parse_args()

    layout = collect_layout()
    validate_layout(layout)
    if args.header is not None:
        write_header(args.header, layout)
        print(f"ok: wrote layout header {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

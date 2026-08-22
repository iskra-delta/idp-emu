#!/usr/bin/env python3
"""Validate a staged, relocatable Iskra Delta Partner release tree."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import tempfile


COMMON_REQUIRED_DIRECTORIES = ("roms", "disks", "assets", "docs")
EXPECTED_CMOS = bytes.fromhex("00 40 80 1b 40 00 90 44")
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
REQUIRED_MEDIA = (
    "partner_cmos.bin",
    "roms/partner_crt.rom",
    "roms/partner_gdp.rom",
    "disks/fdd-partner-p.img",
    "disks/fdd-partner-g.img",
    "disks/hdd-partner-g-system.img",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
        check=False,
    )
    if result.returncode:
        fail(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def verify_png_icon(path: Path, expected_size: int) -> None:
    data = path.read_bytes()
    header = data[:26]
    if len(header) != 26 or header[:8] != b"\x89PNG\r\n\x1a\n":
        fail(f"application icon is not a PNG: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if (width, height) != (expected_size, expected_size):
        fail(
            f"application icon must be {expected_size}x{expected_size}, "
            f"got {width}x{height}: {path}"
        )
    offset = 8
    chunks: set[bytes] = set()
    while offset + 12 <= len(data):
        chunk_size = struct.unpack(">I", data[offset:offset + 4])[0]
        chunks.add(data[offset + 4:offset + 8])
        offset += chunk_size + 12
    if header[25] not in (4, 6) and b"tRNS" not in chunks:
        fail("application icon PNG must carry an alpha channel")


def verify_linux_dependencies(executable: Path, root: Path) -> None:
    output = run(["ldd", str(executable)], root).stdout
    if "not found" in output:
        fail(f"unresolved Linux dependency for {executable}:\n{output}")

    root = root.resolve()
    for line in output.splitlines():
        match = re.search(r"=>\s+(/\S+)", line)
        if not match:
            continue
        dependency = Path(match.group(1)).resolve()
        name = dependency.name
        allowed_system = (
            name.startswith(("libc.so", "libm.so", "libdl.so", "libpthread.so"))
            or name.startswith(("librt.so", "libresolv.so", "libgcc_s.so"))
            or name.startswith(("libGL.so", "libOpenGL.so", "libGLX.so", "libGLdispatch.so"))
            or name.startswith(("libEGL.so", "libdrm.so", "libgbm.so"))
            or name.startswith("libexpat.so")
            or name.startswith("ld-linux")
        )
        if not allowed_system and root not in dependency.parents:
            fail(f"unbundled Linux dependency {dependency} required by {executable}")


def verify_macos_dependencies(executable: Path, root: Path) -> None:
    output = run(["otool", "-L", str(executable)], root).stdout
    for line in output.splitlines()[1:]:
        dependency = line.strip().split(" ", 1)[0]
        if dependency.startswith(("@", "/System/Library/", "/usr/lib/")):
            continue
        if dependency.startswith(str(root.resolve())):
            continue
        fail(f"unbundled macOS dependency {dependency} required by {executable}")


def verify_windows_dependencies(executable: Path, root: Path) -> None:
    nested_dlls = sorted(
        str(path.relative_to(root))
        for path in root.rglob("*.dll")
        if path.parent != root
    )
    if nested_dlls:
        fail(f"Windows DLLs must be beside partner.exe: {nested_dlls}")

    # Loading the executable from an unrelated directory catches missing DLLs.
    # Release CI uses the x64-windows-static vcpkg triplet and static MSVC CRT;
    # dumpbin adds an explicit check when it is available in the runner image.
    dumpbin = shutil.which("dumpbin")
    if not dumpbin:
        return
    output = run([dumpbin, "/DEPENDENTS", str(executable)], root).stdout
    dependencies = {
        line.strip().upper()
        for line in output.splitlines()
        if line.strip().lower().endswith(".dll")
    }
    system32 = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    local_names = {path.name.upper() for path in root.glob("*.dll")}
    unexpected = sorted(
        dependency
        for dependency in dependencies
        if dependency not in local_names and not (system32 / dependency).exists()
    )
    if unexpected:
        fail(f"non-system Windows DLL dependencies: {', '.join(unexpected)}")


def verify_windows_icon(executable: Path) -> None:
    import ctypes

    extract_icon = ctypes.windll.shell32.ExtractIconExW
    extract_icon.argtypes = (
        ctypes.c_wchar_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_uint,
    )
    extract_icon.restype = ctypes.c_uint
    icon_count = extract_icon(str(executable), -1, None, None, 0)
    if icon_count < 1:
        fail(f"Windows executable has no embedded application icon: {executable}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--platform", choices=("linux", "macos", "windows"), required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    required_directories = COMMON_REQUIRED_DIRECTORIES
    if args.platform != "windows":
        required_directories += ("bin", "shared")
    for directory in required_directories:
        if not (root / directory).is_dir():
            fail(f"missing bundle directory: {directory}")
    for relative in REQUIRED_MEDIA:
        path = root / relative
        if not path.is_file() or path.stat().st_size == 0:
            fail(f"missing or empty release media: {relative}")
    for rom in (root / "roms").glob("*.rom"):
        if rom.stat().st_size != 2048:
            fail(f"Partner ROM must be exactly 2048 bytes: {rom}")
    cmos = (root / "partner_cmos.bin").read_bytes()
    if cmos != EXPECTED_CMOS:
        fail("Partner CMOS seed is not the expected eight-byte image")
    for size in ICON_SIZES:
        name = "partner.png" if size == 256 else f"partner-{size}.png"
        verify_png_icon(root / "assets/icons" / name, size)

    suffix = ".exe" if args.platform == "windows" else ""
    if args.platform == "windows":
        gui = root / "partner.exe"
        mcp = root / "idp-mcp.exe"
        for obsolete_directory in ("bin", "shared"):
            if (root / obsolete_directory).exists():
                fail(f"Windows programs and DLLs must not use {obsolete_directory}/")
    else:
        gui = root / "bin" / f"idp-emu{suffix}"
        mcp = root / "bin" / f"idp-mcp{suffix}"
    for executable in (gui, mcp):
        if not executable.is_file():
            fail(f"missing executable: {executable}")
    program_directory = root if args.platform == "windows" else root / "bin"
    unexpected_programs = {
        path.name for path in program_directory.iterdir()
        if path.is_file() and path.suffix.lower() in ("", ".exe")
    } - {gui.name, mcp.name}
    if unexpected_programs:
        fail(f"unexpected runtime programs: {sorted(unexpected_programs)}")

    with tempfile.TemporaryDirectory(prefix="idp-bundle-check-") as temporary:
        work = Path(temporary)
        version = run([str(mcp), "--version"], work).stdout.strip()
        if version != f"idp-mcp {args.version}":
            fail(f"wrong embedded version: {version!r}")
        tools = json.loads(run([str(mcp), "--list-tools"], work).stdout)
        names = {tool.get("name") for tool in tools}
        required_tools = {"status", "run", "step", "measure_cycles", "press_keys", "screen"}
        if not required_tools.issubset(names):
            fail(f"MCP bundle is missing tools: {sorted(required_tools - names)}")
        run([str(gui), "--help"], work)

    checker = {
        "linux": verify_linux_dependencies,
        "macos": verify_macos_dependencies,
        "windows": verify_windows_dependencies,
    }[args.platform]
    for executable in (gui, mcp):
        checker(executable, root)
    if args.platform == "windows":
        verify_windows_icon(gui)

    print(f"verified {args.platform} release tree at {root}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        print(f"bundle verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Validate a staged, relocatable Iskra Delta Partner release tree."""

from __future__ import annotations

import argparse
import hashlib
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
EXPECTED_CMOS = bytes.fromhex("00 00 80 08 40 00 90 4f")
EXPECTED_PAKET_SIZE = 31003
EXPECTED_PAKET_SHA256 = (
    "1f81b6bec27a377f88f99560c060dd06ebe6bb3e243097fcfd36bb667bfff14d"
)
BASIC_CPM_FILES = {
    "CCP.COM",
    "CPM3.SYS",
    "DATE.COM",
    "DEVICE.COM",
    "DIR.COM",
    "DUMP.COM",
    "ED.COM",
    "ERASE.COM",
    "FORMAT.COM",
    "GENCOM.COM",
    "GET.COM",
    "HELP.COM",
    "HELP.HLP",
    "HEXCOM.COM",
    "INITDIR.COM",
    "PAKET.COM",
    "PIP.COM",
    "PUT.COM",
    "RENAME.COM",
    "SAVE.COM",
    "SET.COM",
    "SETDEF.COM",
    "SHOW.COM",
    "SUBMIT.COM",
    "TYPE.COM",
}
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
REQUIRED_MEDIA = (
    "partner_cmos.bin",
    "roms/partner_crt.rom",
    "roms/partner_gdp.rom",
    "disks/hdd-partner-p-system.img",
    "disks/hdd-partner-g-system.img",
)
WINDOWS_LAUNCHERS = {
    "partnerp.bat": "--model crt --system-crt-hdd",
    "partnerg.bat": "--model gdp --system-hdd",
}
PACKAGED_DISK_IMAGES = {
    "hdd-partner-p-system.img",
    "hdd-partner-g-system.img",
}


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


def verify_ico_icon(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 6:
        fail(f"application ICO is truncated: {path}")
    reserved, image_type, count = struct.unpack("<HHH", data[:6])
    if reserved != 0 or image_type != 1 or count != len(ICON_SIZES):
        fail(f"invalid multi-resolution application ICO: {path}")
    if len(data) < 6 + count * 16:
        fail(f"application ICO directory is truncated: {path}")
    sizes: set[int] = set()
    for index in range(count):
        offset = 6 + index * 16
        width = data[offset] or 256
        height = data[offset + 1] or 256
        if width != height:
            fail(f"application ICO contains a non-square image: {path}")
        sizes.add(width)
    if sizes != set(ICON_SIZES):
        fail(f"application ICO has wrong image sizes {sorted(sizes)}: {path}")


def extract_cpm_file(
    path: Path,
    directory_offset: int,
    directory_entries: int,
    expected_name: str,
    block_size: int,
    extent_mask: int = 0,
    user: int = 0,
) -> bytes:
    image = path.read_bytes()
    expected = expected_name.upper()
    extents: list[tuple[int, bytes]] = []
    for index in range(directory_entries):
        offset = directory_offset + index * 32
        entry = image[offset:offset + 32]
        if len(entry) != 32 or entry[0] != user:
            continue
        name = "".join(chr(value & 0x7F) for value in entry[1:9]).rstrip()
        suffix = "".join(chr(value & 0x7F) for value in entry[9:12]).rstrip()
        actual = f"{name}.{suffix}" if suffix else name
        if actual != expected:
            continue
        raw_extent = entry[12]
        extent_number = (raw_extent & ~extent_mask) | ((entry[14] & 0x3F) << 5)
        remaining = ((raw_extent & extent_mask) * 128 + entry[15]) * 128
        extent = bytearray()
        for allocation_offset in range(16, 32, 2):
            block = int.from_bytes(
                entry[allocation_offset:allocation_offset + 2], "little")
            if block == 0 or remaining == 0:
                break
            block_offset = directory_offset + block * block_size
            take = min(block_size, remaining)
            extent.extend(image[block_offset:block_offset + take])
            remaining -= take
        if remaining != 0:
            fail(f"CP/M file {expected} has a truncated extent in {path}")
        extents.append((extent_number, bytes(extent)))
    if not extents:
        fail(f"CP/M user {user} file {expected} is missing from {path}")
    extents.sort(key=lambda item: item[0])
    expected_extent = 0
    output = bytearray()
    for extent_number, data in extents:
        if extent_number != expected_extent:
            fail(
                f"CP/M file {expected} has an overlapping or missing "
                f"extent in {path}"
            )
        output.extend(data)
        expected_extent += (len(data) + 16383) // 16384
    return bytes(output)


def cpm_directory_files(
    path: Path,
    directory_offset: int,
    directory_entries: int,
) -> set[tuple[int, str]]:
    image = path.read_bytes()
    files: set[tuple[int, str]] = set()
    for index in range(directory_entries):
        offset = directory_offset + index * 32
        entry = image[offset:offset + 32]
        if len(entry) != 32 or entry[0] > 15:
            continue
        name = "".join(chr(value & 0x7F) for value in entry[1:9]).rstrip()
        suffix = "".join(chr(value & 0x7F) for value in entry[9:12]).rstrip()
        if not name:
            continue
        files.add((entry[0], f"{name}.{suffix}" if suffix else name))
    return files


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
    packaged_dlls = sorted(
        str(path.relative_to(root))
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() == ".dll"
    )
    if packaged_dlls:
        fail(
            "the static Windows release must not package operating-system "
            f"DLLs: {packaged_dlls}"
        )

    # Release CI uses the x64-windows-static vcpkg triplet and static MSVC CRT.
    # Every remaining import must therefore be supplied by Windows itself.
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
    unexpected = sorted(
        dependency
        for dependency in dependencies
        if not (system32 / dependency).exists()
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
    packaged_images = {path.name for path in (root / "disks").iterdir()}
    if packaged_images != PACKAGED_DISK_IMAGES:
        fail(
            "release must contain only the two system hard disks; "
            f"found={sorted(packaged_images)}"
        )
    # Extract and hash the universal client from both model-specific system
    # media. This prevents either package profile from silently reverting to
    # a missing, stale, or display-specific PAKET.COM.
    packaged_pakets = (
        extract_cpm_file(
            root / "disks/hdd-partner-p-system.img",
            1 * 32 * 256,
            1024,
            "PAKET.COM",
            4096,
            extent_mask=1,
        ),
        extract_cpm_file(
            root / "disks/hdd-partner-g-system.img",
            1 * 32 * 256,
            1024,
            "PAKET.COM",
            4096,
            extent_mask=1,
        ),
    )
    for packaged_paket in packaged_pakets:
        if len(packaged_paket) < EXPECTED_PAKET_SIZE:
            fail("packaged PAKET.COM is truncated")
        packaged_paket = packaged_paket[:EXPECTED_PAKET_SIZE]
        if hashlib.sha256(packaged_paket).hexdigest() != EXPECTED_PAKET_SHA256:
            fail("CP/M boot media contains the wrong PAKET.COM build")
    expected_cpm_files = {(0, name) for name in BASIC_CPM_FILES}
    for relative in (
        "disks/hdd-partner-p-system.img",
        "disks/hdd-partner-g-system.img",
    ):
        actual_cpm_files = cpm_directory_files(
            root / relative,
            1 * 32 * 256,
            1024,
        )
        if actual_cpm_files != expected_cpm_files:
            missing = sorted(expected_cpm_files - actual_cpm_files)
            unexpected = sorted(actual_cpm_files - expected_cpm_files)
            fail(
                f"basic CP/M manifest mismatch in {relative}; "
                f"missing={missing}, unexpected={unexpected}"
            )
    for rom in (root / "roms").glob("*.rom"):
        if rom.stat().st_size != 2048:
            fail(f"Partner ROM must be exactly 2048 bytes: {rom}")
    cmos = (root / "partner_cmos.bin").read_bytes()
    if cmos != EXPECTED_CMOS:
        fail("Partner CMOS seed is not the expected eight-byte image")
    for size in ICON_SIZES:
        name = "partner.png" if size == 256 else f"partner-{size}.png"
        verify_png_icon(root / "assets/icons" / name, size)
        mcp_name = "mcp.png" if size == 256 else f"mcp-{size}.png"
        verify_png_icon(root / "assets/icons" / mcp_name, size)
    verify_ico_icon(root / "assets/icons/partner.ico")
    verify_ico_icon(root / "assets/icons/mcp.ico")

    suffix = ".exe" if args.platform == "windows" else ""
    if args.platform == "windows":
        gui = root / "partner.exe"
        mcp = root / "idp-mcp.exe"
        for obsolete_directory in ("bin", "shared"):
            if (root / obsolete_directory).exists():
                fail(f"Windows programs and DLLs must not use {obsolete_directory}/")
        for name, arguments in WINDOWS_LAUNCHERS.items():
            launcher = root / name
            if not launcher.is_file():
                fail(f"missing Windows launcher: {launcher}")
            launcher_text = launcher.read_text(encoding="utf-8")
            expected_command = f'"%~dp0partner.exe" {arguments} %*'
            if expected_command not in launcher_text:
                fail(f"Windows launcher has wrong command: {launcher}")
        for obsolete in ("partner-classic.bat", "partner-graphical.bat"):
            if (root / obsolete).exists():
                fail(f"obsolete Windows launcher remains: {obsolete}")
    else:
        gui = root / "bin" / f"idp-emu{suffix}"
        mcp = root / "bin" / f"idp-mcp{suffix}"
    if args.platform in ("linux", "macos"):
        partnerp = root / "bin" / "partnerp"
        partnerg = root / "bin" / "partnerg"
        for path in (partnerp, partnerg):
            if not path.is_file() or path.stat().st_size == 0:
                fail(f"missing Unix launcher: {path}")
        partnerp_text = partnerp.read_text(encoding="utf-8")
        partnerg_text = partnerg.read_text(encoding="utf-8")
        for profile_argument in ("--model crt", "--system-crt-hdd"):
            if profile_argument not in partnerp_text:
                fail(f"Partner P launcher is missing {profile_argument}")
        for profile_argument in ("--model gdp", "--system-hdd"):
            if profile_argument not in partnerg_text:
                fail(f"Partner G launcher is missing {profile_argument}")
        if (root / "bin" / "partner").exists():
            fail("obsolete Unix partner launcher remains")
    obsolete_squid_paths = (
        root / "bin/squid-server",
        root / "squid",
    )
    for path in obsolete_squid_paths:
        if path.exists():
            fail(f"obsolete external Squid component is still packaged: {path}")
    for executable in (gui, mcp):
        if not executable.is_file():
            fail(f"missing executable: {executable}")
    program_directory = root if args.platform == "windows" else root / "bin"
    expected_programs = {gui.name, mcp.name}
    if args.platform in ("linux", "macos"):
        expected_programs.update({"partnerp", "partnerg"})
    unexpected_programs = {
        path.name for path in program_directory.iterdir()
        if path.is_file() and path.suffix.lower() in ("", ".exe")
    } - expected_programs
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
        gui_help = run([str(gui), "--help"], work)
        help_text = gui_help.stdout + gui_help.stderr
        for option in (
            "--system-crt-hdd",
            "--system-hdd",
            "--sio-squid",
        ):
            if option not in help_text:
                fail(f"GUI bundle is missing shortcut media option: {option}")
        if args.platform in ("linux", "macos"):
            for launcher in (partnerp, partnerg):
                run([str(launcher), "--help"], work)

    checker = {
        "linux": verify_linux_dependencies,
        "macos": verify_macos_dependencies,
        "windows": verify_windows_dependencies,
    }[args.platform]
    for executable in (gui, mcp):
        checker(executable, root)
    if args.platform == "windows":
        verify_windows_icon(gui)
        verify_windows_icon(mcp)

    print(f"verified {args.platform} release tree at {root}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        print(f"bundle verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)

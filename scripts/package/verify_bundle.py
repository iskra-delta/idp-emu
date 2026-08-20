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
import sys
import tempfile


REQUIRED_DIRECTORIES = ("bin", "shared", "roms", "disks", "assets", "docs")
REQUIRED_MEDIA = (
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
    local_names = {
        path.name.upper()
        for directory in (root / "bin", root / "shared")
        for path in directory.glob("*.dll")
    }
    unexpected = sorted(
        dependency
        for dependency in dependencies
        if dependency not in local_names and not (system32 / dependency).exists()
    )
    if unexpected:
        fail(f"non-system Windows DLL dependencies: {', '.join(unexpected)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--platform", choices=("linux", "macos", "windows"), required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    for directory in REQUIRED_DIRECTORIES:
        if not (root / directory).is_dir():
            fail(f"missing bundle directory: {directory}")
    for relative in REQUIRED_MEDIA:
        path = root / relative
        if not path.is_file() or path.stat().st_size == 0:
            fail(f"missing or empty release media: {relative}")
    for rom in (root / "roms").glob("*.rom"):
        if rom.stat().st_size != 2048:
            fail(f"Partner ROM must be exactly 2048 bytes: {rom}")

    suffix = ".exe" if args.platform == "windows" else ""
    gui = root / "bin" / f"idp-emu{suffix}"
    mcp = root / "bin" / f"idp-mcp{suffix}"
    for executable in (gui, mcp):
        if not executable.is_file():
            fail(f"missing executable: {executable}")
    unexpected_programs = {
        path.name for path in (root / "bin").iterdir()
        if path.is_file()
    } - {gui.name, mcp.name}
    if unexpected_programs:
        fail(f"unexpected files in runtime bin/: {sorted(unexpected_programs)}")

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

    print(f"verified {args.platform} release tree at {root}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        print(f"bundle verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)

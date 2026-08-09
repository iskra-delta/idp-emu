#!/usr/bin/env python3
"""Boot and launch the Partner software floppy-image compatibility matrix.

The probe boots the original Partner GDP ROM from the small CP/M system HDD,
mounts each software image as drive B:, launches a program from that drive,
and captures both diagnostics and the final composite framebuffer.

The two ``*-xcc-final.img`` artifacts in the current source trees have valid
CP/M geometry but empty directories.  They are reported as empty artifacts;
with ``--test-xcc-binaries`` the companion COM files are copied into temporary
images so the binaries can still be exercised without modifying their repos.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[1]
PROJECTS = REPO.parent


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    image: Path
    command: str
    description: str
    companion_com: Path | None = None
    staged_name: str | None = None
    min_pixels: int = 1


CASES = (
    Case("kamenje", PROJECTS / "aids/bin/kamenje.img", "kamenje", "Kamenje", min_pixels=20_000),
    Case("advent", PROJECTS / "idp-games/bin/idp-games.img", "advent", "Adventure", min_pixels=2_000),
    Case("frogger", PROJECTS / "idp-games/bin/idp-games.img", "frogger", "Frogger", min_pixels=10_000),
    Case("invaders", PROJECTS / "idp-games/bin/idp-games.img", "invaders", "Invaders", min_pixels=1_500),
    Case("tetris", PROJECTS / "idp-games/bin/idp-games.img", "tetris", "Tetris", min_pixels=4_000),
    Case("tetrisg", PROJECTS / "idp-games/bin/idp-games.img", "tetrisg", "Tetris G", min_pixels=100_000),
    Case("ntenis", PROJECTS / "idp-pong/bin/ntenis.img", "ntenis", "Namizni tenis", min_pixels=8_000),
    Case("eurorun", PROJECTS / "idp-quill/bin/fddb.img", "eurorun", "Eurorun", min_pixels=2_500),
    Case("kontra", PROJECTS / "idp-quill/bin/fddb.img", "kontra", "Kontrabant", min_pixels=7_000),
    Case("kontra2", PROJECTS / "idp-quill/bin/fddb.img", "kontra2", "Kontrabant II", min_pixels=5_000),
    Case("lunatik", PROJECTS / "lunatik/bin/fddb.img", "lunatik", "Lunatik", min_pixels=3_000),
    Case("sah", PROJECTS / "sah-partner/bin/fddb.img", "sah", "Sah Partner", min_pixels=4_000),
    Case("ura", PROJECTS / "ura/bin/fddb.img", "ura", "Ura", min_pixels=7_000),
)


XCC_CASES = (
    Case(
        "kamenje-xcc-final",
        PROJECTS / "aids/bin/kamenje-xcc-final.img",
        "kamxcc",
        "Kamenje XCC final (temporary populated image)",
        PROJECTS / "aids/bin/kamenje-xcc-final.com",
        "KAMXCC.COM",
        min_pixels=20_000,
    ),
    Case(
        "ntenis-xcc-final",
        PROJECTS / "idp-pong/bin/ntenis-xcc-final.img",
        "ntxcc",
        "Namizni tenis XCC final (temporary populated image)",
        PROJECTS / "idp-pong/bin/ntenis-xcc-final.com",
        "NTXCC.COM",
        min_pixels=3_000,
    ),
)


EMPTY_IMAGE_SHA256 = "02f5208f207b46429618ad2a20120ff509cbd495d7ad92c0e038057c094c854d"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pgm_nonblack_pixels(path: Path) -> int:
    data = path.read_bytes()
    match = re.match(rb"P5\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not match:
        raise ValueError(f"unsupported PGM header in {path}")
    pixels = data[match.end():]
    return sum(value != 0 for value in pixels)


def stage_xcc_case(case: Case, output_dir: Path) -> Path:
    if case.companion_com is None or case.staged_name is None:
        return case.image
    image = output_dir / f"{case.name}.img"
    staged_com = output_dir / case.staged_name
    shutil.copy2(case.image, image)
    shutil.copy2(case.companion_com, staged_com)
    command = [
        "docker", "run", "--rm",
        "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{output_dir}:/matrix",
        "wischner/xcc-z80-idp:latest",
        "cpmdisk", "add", f"/matrix/{image.name}",
        "-f", "fdd", "-u", "0", f"/matrix/{staged_com.name}",
    ]
    subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return image


def parse_probe_output(output: str) -> dict[str, int | str | bool]:
    result: dict[str, int | str | bool] = {}
    patterns = {
        "pc": r"^pc=([0-9a-fA-F]+) ticks=(\d+)$",
        "transient": r"^transient_entries=(\d+) last_tick=(\d+)$",
        "fdc": r"^fdc .*last_read_ok=(\d+).*dma_reads=(\d+) dma_writes=(\d+)$",
        "ef_io": r"^io ef\(r/w\)=(\d+)/(\d+)",
        "ef_bits": r"^ef_page_bits=(\d+)/(\d+)$",
    }
    for line in output.splitlines():
        if match := re.match(patterns["pc"], line):
            result["pc"] = match.group(1).lower()
            result["ticks"] = int(match.group(2))
        elif match := re.match(patterns["transient"], line):
            result["transient_entries"] = int(match.group(1))
            result["last_transient_tick"] = int(match.group(2))
        elif match := re.match(patterns["fdc"], line):
            result["last_read_ok"] = bool(int(match.group(1)))
            result["dma_reads"] = int(match.group(2))
            result["dma_writes"] = int(match.group(3))
        elif match := re.match(patterns["ef_io"], line):
            result["ef_reads"] = int(match.group(1))
            result["ef_writes"] = int(match.group(2))
        elif match := re.match(patterns["ef_bits"], line):
            result["ef_page0_bits"] = int(match.group(1))
            result["ef_page1_bits"] = int(match.group(2))
    return result


def run_case(case: Case, image: Path, output_dir: Path, max_ticks: int) -> dict[str, object]:
    started = time.monotonic()
    log = output_dir / f"{case.name}.log"
    pgm = output_dir / f"{case.name}.pgm"
    nvram = output_dir / f"{case.name}.nvram"
    source_nvram = REPO / "partner_cmos.bin"
    if source_nvram.exists():
        shutil.copy2(source_nvram, nvram)
    else:
        nvram.write_bytes(bytes.fromhex("00 40 80 1b 40 00 90 44"))

    env = os.environ.copy()
    env.update({
        "IDP_HDD": str(REPO / "disks/hdd-partner-g-system.img"),
        "IDP_FLOPPY": str(image),
        "IDP_NVRAM": str(nvram),
        "IDP_FIXED_RTC": "946684800",
        "IDP_SCRIPT": f"b:\r{case.command}\r",
        # The original ROM is still completing its destructive RAM test near
        # tick 45M.  Feeding the command there can make those bytes look like
        # keyboard data to the monitor instead of CP/M input.  Wait until the
        # HDD boot and CCP prompt are stable before selecting drive B:.
        "IDP_SCRIPT_START_TICK": "80000000",
        "IDP_SCRIPT_INTERVAL_TICKS": "200000",
        "IDP_SCRIPT_CR_DELAY_TICKS": "8000000",
        "IDP_MAX_TICKS": str(max_ticks),
        "IDP_DUMP_PGM": str(pgm),
    })
    completed = subprocess.run(
        [str(REPO / "bin/idp-gdp-probe")],
        cwd=REPO,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    log.write_text(completed.stdout, encoding="utf-8")
    parsed = parse_probe_output(completed.stdout)
    parsed.update({
        "name": case.name,
        "description": case.description,
        "image": str(case.image),
        "mounted_image": str(image),
        "command": case.command,
        "returncode": completed.returncode,
        "seconds": round(time.monotonic() - started, 3),
        "log": str(log),
        "framebuffer": str(pgm),
        "minimum_framebuffer_pixels": case.min_pixels,
    })
    if pgm.exists():
        parsed["framebuffer_nonblack_pixels"] = pgm_nonblack_pixels(pgm)
        parsed["framebuffer_sha256"] = sha256(pgm)

    launched = int(parsed.get("transient_entries", 0)) >= 3
    read_ok = bool(parsed.get("last_read_ok", False))
    visible = int(parsed.get("framebuffer_nonblack_pixels", 0)) >= case.min_pixels
    parsed["launched"] = launched
    parsed["meaningful_frame"] = visible
    parsed["passed"] = completed.returncode == 0 and launched and read_ok and visible
    return parsed


def write_reports(output_dir: Path, results: list[dict[str, object]], artifacts: list[dict[str, object]]) -> None:
    report = {"results": results, "artifacts": artifacts}
    (output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Partner software compatibility matrix",
        "",
        "| Case | Launch | FDC read | Meaningful frame | EF writes | Pixels | Result |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for item in results:
        lines.append(
            "| {name} | {launch} | {fdc} | {visible} | {ef} | {pixels} | {status} |".format(
                name=item["name"],
                launch="yes" if item.get("launched") else "no",
                fdc="yes" if item.get("last_read_ok") else "no",
                visible="yes" if item.get("meaningful_frame") else "no",
                ef=item.get("ef_writes", "?"),
                pixels=item.get("framebuffer_nonblack_pixels", "?"),
                status="PASS" if item.get("passed") else "FAIL",
            )
        )
    if artifacts:
        lines.extend(["", "## Source-image artifacts", ""])
        for artifact in artifacts:
            lines.append(
                f"- `{artifact['path']}`: {artifact['status']} "
                f"(sha256 `{artifact['sha256']}`)"
            )
    lines.append("")
    (output_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/idp-partner-software-matrix"))
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--max-ticks", type=int, default=180_000_000)
    parser.add_argument("--case", action="append", dest="selected", help="run only this named case")
    parser.add_argument(
        "--test-xcc-binaries",
        action="store_true",
        help="populate temporary copies of the two empty XCC images and test their companion binaries",
    )
    parser.add_argument("--no-build", action="store_true", help="do not build idp-gdp-probe first")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        subprocess.run(
            ["cmake", "--build", "build", "--target", "idp-gdp-probe", "-j"],
            cwd=REPO,
            check=True,
        )

    artifacts: list[dict[str, object]] = []
    for case in XCC_CASES:
        if not case.image.exists():
            status = "missing"
            digest = "-"
        else:
            digest = sha256(case.image)
            status = "empty CP/M image" if digest == EMPTY_IMAGE_SHA256 else "non-empty image"
        artifacts.append({"path": str(case.image), "status": status, "sha256": digest})

    cases = list(CASES)
    if args.test_xcc_binaries:
        cases.extend(XCC_CASES)
    if args.selected:
        selected = set(args.selected)
        cases = [case for case in cases if case.name in selected]
        unknown = selected.difference(case.name for case in cases)
        if unknown:
            parser.error(f"unknown case(s): {', '.join(sorted(unknown))}")

    missing = [str(case.image) for case in cases if not case.image.exists()]
    missing.extend(str(case.companion_com) for case in cases if case.companion_com and not case.companion_com.exists())
    if missing:
        print("Missing compatibility input(s):", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 2

    staged = {case.name: stage_xcc_case(case, args.output) for case in cases}
    results: list[dict[str, object]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = {
            executor.submit(run_case, case, staged[case.name], args.output, args.max_ticks): case
            for case in cases
        }
        for future in concurrent.futures.as_completed(futures):
            case = futures[future]
            try:
                result = future.result()
            except Exception as exc:
                result = {
                    "name": case.name,
                    "description": case.description,
                    "image": str(case.image),
                    "passed": False,
                    "error": str(exc),
                }
            results.append(result)
            state = "PASS" if result.get("passed") else "FAIL"
            print(f"[{state}] {case.name} ({result.get('seconds', '?')} s)", flush=True)

    order = {case.name: index for index, case in enumerate(cases)}
    results.sort(key=lambda item: order[str(item["name"])])
    write_reports(args.output, results, artifacts)
    passed = sum(bool(result.get("passed")) for result in results)
    print(f"{passed}/{len(results)} cases passed; report: {args.output / 'report.md'}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())

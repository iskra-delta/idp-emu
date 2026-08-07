#!/usr/bin/env python3
"""Make xcc IX-frame prologues safe for PartOS preemption.

xcc sometimes spills register arguments to -N(ix) before it lowers sp to
reserve the frame. That is fine on a non-preemptive target, but PartOS takes
timer interrupts on the current thread stack; an interrupt in that tiny window
pushes the return PC/context over those not-yet-reserved locals.

This pass only rewrites the simple prologue shape:

    push ix
    ld ix,#0
    add ix,sp
    ld -N(ix),r
    ...
    ld hl,#-FRAME
    add hl,sp
    ld sp,hl

into the same code with equivalent `dec sp` reservations immediately after
`add ix,sp`. We do not move xcc's own `ld hl,#-FRAME` upward because that would
clobber an incoming HL argument before xcc spills it. Prologues that do
anything more complex before reserving the frame are left untouched.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ADD_IX_SP_RE = re.compile(r"^\s*add\s+ix\s*,\s*sp\s*(?:;.*)?$", re.IGNORECASE)
ALLOC_RE = re.compile(r"^(\s*)ld\s+hl\s*,\s*#-(\d+)\s*(?:;.*)?$", re.IGNORECASE)
ADD_HL_SP_RE = re.compile(r"^\s*add\s+hl\s*,\s*sp\s*(?:;.*)?$", re.IGNORECASE)
LD_SP_HL_RE = re.compile(r"^\s*ld\s+sp\s*,\s*hl\s*(?:;.*)?$", re.IGNORECASE)
IX_SPILL_RE = re.compile(
    r"^\s*ld\s+-\d+\(ix\)\s*,\s*(?:a|b|c|d|e|h|l)\s*(?:;.*)?$",
    re.IGNORECASE,
)


def is_comment_or_blank(line: str) -> bool:
    stripped = line.strip()
    return stripped == "" or stripped.startswith(";")


def fix_lines(lines: list[str]) -> tuple[list[str], int]:
    out: list[str] = []
    rewrites = 0
    i = 0

    while i < len(lines):
        out.append(lines[i])
        if not ADD_IX_SP_RE.match(lines[i]):
            i += 1
            continue

        j = i + 1
        saw_spill = False
        safe_to_move = True

        while j + 2 < len(lines):
            if (
                ALLOC_RE.match(lines[j])
                and ADD_HL_SP_RE.match(lines[j + 1])
                and LD_SP_HL_RE.match(lines[j + 2])
            ):
                break
            if is_comment_or_blank(lines[j]):
                j += 1
                continue
            if IX_SPILL_RE.match(lines[j]):
                saw_spill = True
                j += 1
                continue
            safe_to_move = False
            break

        if (
            safe_to_move
            and saw_spill
            and j + 2 < len(lines)
            and ALLOC_RE.match(lines[j])
            and ADD_HL_SP_RE.match(lines[j + 1])
            and LD_SP_HL_RE.match(lines[j + 2])
        ):
            indent = ALLOC_RE.match(lines[j]).group(1)
            frame_size = int(ALLOC_RE.match(lines[j]).group(2), 10)
            out.append(
                f"{indent}; xcc-fix: reserve frame before -N(ix) spills\n"
            )
            out.extend(f"{indent}dec\tsp\n" for _ in range(frame_size))
            out.extend(lines[i + 1 : j])
            rewrites += 1
            i = j + 3
            continue

        i += 1

    return out, rewrites


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="print the number of rewritten prologues",
    )
    args = parser.parse_args()

    lines = args.input.read_text().splitlines(keepends=True)
    fixed, rewrites = fix_lines(lines)
    args.output.write_text("".join(fixed))
    if args.verbose:
        print(f"{args.input}: rewrote {rewrites} xcc prologue(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

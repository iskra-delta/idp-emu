#!/usr/bin/env python3
"""Create a Partner CRT system hard disk from the Partner GDP image.

The Partner W hard-disk CP/M BIOS already contains physical drivers for both
the CRT serial terminal and the GDP display. Convert its console vectors and
startup path to the CRT hardware while leaving the boot loader, hard-disk
geometry, CP/M files, and utilities unchanged.
"""

from __future__ import annotations

import argparse
from pathlib import Path


# BIOS startup code after relocation:
#   LD HL,8000h / LD (@CIVEC),HL  ; CRT console input
#   LD HL,0400h / LD (@COVEC),HL  ; GDP console output
#   LD HL,4000h / LD (@LOVEC),HL  ; printer output
GDP_CONSOLE_INITIALIZER = bytes.fromhex(
    "21 00 80 22 be f9 21 00 04 22 c0 f9 21 00 40 22 c6 f9"
)
CRT_CONSOLE_INITIALIZER = bytes.fromhex(
    "21 00 80 22 be f9 21 00 80 22 c0 f9 21 00 40 22 c6 f9"
)

# The GDP character-device initializer enters a large SETUP routine which
# waits on EF9367 and AVDC status transitions.  Make that routine return
# immediately; its caller then completes the normal CINIT return path.  This is
# the same semantic difference as the original CRT system, which has no GDP
# SETUP.
GDP_SETUP_INITIALIZER = bytes.fromhex(
    "cd 3d ad cd f5 ac cd 1a ac 3e 01 32 8a ff "
    "3e 18 32 8b ff 3e 65 d3 32 21 df b5 22 e2 b5"
)
CRT_SIO_RECEIVE_SETUP = bytes.fromhex("3e 01 d3 d9 3e 10 d3 d9 c9")
DISABLED_GDP_SETUP_INITIALIZER = (
    CRT_SIO_RECEIVE_SETUP + GDP_SETUP_INITIALIZER[len(CRT_SIO_RECEIVE_SETUP):]
)

# The common BIOS reset code resets all four SIO channels.  The GDP system
# later configures its graphical console instead of restoring SIO1 channel A.
# On the CRT machine that channel is the built-in terminal and has already
# been configured by CPMLDR.  Preserve it while retaining resets for the three
# peripheral channels.
SIO_CHANNEL_RESET = bytes.fromhex("3e 18 d3 db d3 e3 d3 d9 d3 e1")
CRT_SIO_CHANNEL_RESET = bytes.fromhex("3e 18 d3 db d3 e3 00 00 d3 e1")


def replace_unique(image: bytearray, source: bytes, replacement: bytes,
                   description: str) -> int:
    matches: list[int] = []
    offset = 0
    while True:
        offset = image.find(source, offset)
        if offset < 0:
            break
        matches.append(offset)
        offset += 1

    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one {description}, found {len(matches)}"
        )

    patch_offset = matches[0]
    image[patch_offset : patch_offset + len(source)] = replacement
    return patch_offset


def build_image(source: Path, output: Path) -> None:
    image = bytearray(source.read_bytes())
    console_offset = replace_unique(
        image, GDP_CONSOLE_INITIALIZER, CRT_CONSOLE_INITIALIZER,
        "GDP console-vector initializer",
    )
    setup_offset = replace_unique(
        image, GDP_SETUP_INITIALIZER, DISABLED_GDP_SETUP_INITIALIZER,
        "GDP SETUP initializer",
    )
    sio_offset = replace_unique(
        image, SIO_CHANNEL_RESET, CRT_SIO_CHANNEL_RESET,
        "SIO channel-reset sequence",
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    print(
        f"created {output}: routed CP/M console output from GDP to CRT "
        f"at image offset 0x{console_offset:x}; disabled GDP SETUP "
        f"at 0x{setup_offset:x}; preserved CRT SIO at 0x{sio_offset:x}"
        "; enabled CRT receive interrupts in place of GDP SETUP"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source",
        nargs="?",
        type=Path,
        default=Path("disks/hdd-partner-g-system.img"),
    )
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("disks/hdd-partner-p-system.img"),
    )
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must be different files")
    build_image(args.source, args.output)


if __name__ == "__main__":
    main()

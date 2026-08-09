#!/usr/bin/env python3
"""Build a minimal Partner GDP CP/M hard-disk image.

The Partner image uses a CP/M directory at 0x2000 with 1024 32-byte entries
and 4 KiB allocation blocks.  Preserve the boot area and a small user-0
system toolset, remove every other file entry, and zero all released blocks.
"""

from __future__ import annotations

import argparse
from pathlib import Path


DIRECTORY_OFFSET = 0x2000
DIRECTORY_ENTRIES = 1024
DIRECTORY_ENTRY_SIZE = 32
ALLOCATION_BLOCK_SIZE = 0x1000
FIRST_DATA_BLOCK = 8

KEEP_USER_0 = {
    "CCP.COM",
    "CPM3.SYS",
    "DIR.COM",
    "ED.COM",
    "ERASE.COM",
    "PIP.COM",
    "SET.COM",
    "SUBMIT.COM",
    "TYPE.COM",
}


def entry_filename(entry: bytes) -> str:
    name = "".join(chr(value & 0x7F) for value in entry[1:9]).rstrip()
    suffix = "".join(chr(value & 0x7F) for value in entry[9:12]).rstrip()
    return f"{name}.{suffix}" if suffix else name


def allocation_blocks(entry: bytes) -> set[int]:
    blocks: set[int] = set()
    for offset in range(16, 32, 2):
        block = int.from_bytes(entry[offset : offset + 2], "little")
        if block:
            blocks.add(block)
    return blocks


def build_image(source: Path, output: Path) -> None:
    image = bytearray(source.read_bytes())
    if len(image) <= DIRECTORY_OFFSET + DIRECTORY_ENTRIES * DIRECTORY_ENTRY_SIZE:
        raise ValueError(f"image is too small: {source}")
    if (len(image) - DIRECTORY_OFFSET) % ALLOCATION_BLOCK_SIZE:
        raise ValueError(f"image size is not aligned to the Partner allocation map: {source}")

    kept_names: set[str] = set()
    kept_blocks: set[int] = set()
    removed_files = 0

    for index in range(DIRECTORY_ENTRIES):
        offset = DIRECTORY_OFFSET + index * DIRECTORY_ENTRY_SIZE
        entry = bytes(image[offset : offset + DIRECTORY_ENTRY_SIZE])
        user = entry[0]

        if user == 0xE5:
            continue
        if user > 31:
            # Preserve CP/M 3 label and timestamp directory records.
            continue

        filename = entry_filename(entry)
        if user == 0 and filename in KEEP_USER_0:
            kept_names.add(filename)
            kept_blocks.update(allocation_blocks(entry))
            continue

        image[offset : offset + DIRECTORY_ENTRY_SIZE] = b"\xE5" + bytes(DIRECTORY_ENTRY_SIZE - 1)
        removed_files += 1

    missing = KEEP_USER_0 - kept_names
    if missing:
        raise ValueError(f"source image is missing required files: {', '.join(sorted(missing))}")

    allocation_block_count = (len(image) - DIRECTORY_OFFSET) // ALLOCATION_BLOCK_SIZE
    for block in range(FIRST_DATA_BLOCK, allocation_block_count):
        if block in kept_blocks:
            continue
        offset = DIRECTORY_OFFSET + block * ALLOCATION_BLOCK_SIZE
        image[offset : offset + ALLOCATION_BLOCK_SIZE] = bytes(ALLOCATION_BLOCK_SIZE)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    print(
        f"created {output}: kept {len(kept_names)} user-0 files, "
        f"removed {removed_files} files, retained {len(kept_blocks)} data blocks"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source",
        nargs="?",
        type=Path,
        default=Path("disks/hdd-partner-g.img"),
    )
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("disks/hdd-partner-g-system.img"),
    )
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must be different files")
    build_image(args.source, args.output)


if __name__ == "__main__":
    main()

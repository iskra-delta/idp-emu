#!/usr/bin/env python3
"""Build a basic Partner GDP CP/M hard-disk image.

The Partner image uses a CP/M directory at 0x2000 with 1024 32-byte entries
and 4 KiB allocation blocks with EXM=1. Preserve the boot area and the normal
CP/M Plus maintenance tools, remove every other file entry, zero all released
blocks, and optionally add explicitly requested files such as PAKET.COM.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import NamedTuple


DIRECTORY_OFFSET = 0x2000
DIRECTORY_ENTRIES = 1024
DIRECTORY_ENTRY_SIZE = 32
ALLOCATION_BLOCK_SIZE = 0x1000
FIRST_DATA_BLOCK = 8
CPM_RECORD_SIZE = 128
LOGICAL_EXTENT_RECORDS = 128
DIRECTORY_EXTENT_RECORDS = 256

KEEP_USER_0 = {
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


class AddedFile(NamedTuple):
    name: str
    path: Path


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


def split_cpm_filename(filename: str) -> tuple[str, str]:
    parts = filename.upper().split(".")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8:
        raise ValueError(f"invalid CP/M filename: {filename}")
    suffix = parts[1] if len(parts) == 2 else ""
    if len(suffix) > 3:
        raise ValueError(f"invalid CP/M filename: {filename}")
    return parts[0], suffix


def add_user_zero_file(
    image: bytearray,
    added: AddedFile,
    used_blocks: set[int],
) -> None:
    name, suffix = split_cpm_filename(added.name)
    payload = added.path.read_bytes()
    record_count = (len(payload) + CPM_RECORD_SIZE - 1) // CPM_RECORD_SIZE
    entry_count = max(
        1,
        (record_count + DIRECTORY_EXTENT_RECORDS - 1) //
        DIRECTORY_EXTENT_RECORDS,
    )
    free_entries = [
        index for index in range(DIRECTORY_ENTRIES)
        if image[DIRECTORY_OFFSET + index * DIRECTORY_ENTRY_SIZE] == 0xE5
    ]
    if len(free_entries) < entry_count:
        raise ValueError(f"not enough directory entries for {added.name}")

    allocation_block_count = (
        len(image) - DIRECTORY_OFFSET
    ) // ALLOCATION_BLOCK_SIZE
    free_blocks = [
        block for block in range(FIRST_DATA_BLOCK, allocation_block_count)
        if block not in used_blocks
    ]
    required_blocks = (
        record_count * CPM_RECORD_SIZE + ALLOCATION_BLOCK_SIZE - 1
    ) // ALLOCATION_BLOCK_SIZE
    if len(free_blocks) < required_blocks:
        raise ValueError(f"not enough allocation blocks for {added.name}")

    padded = payload + bytes([0x1A]) * (
        record_count * CPM_RECORD_SIZE - len(payload)
    )
    payload_offset = 0
    block_offset = 0
    remaining_records = record_count
    for extent_index in range(entry_count):
        extent_records = min(DIRECTORY_EXTENT_RECORDS, remaining_records)
        extent_bytes = extent_records * CPM_RECORD_SIZE
        extent_blocks = (
            extent_bytes + ALLOCATION_BLOCK_SIZE - 1
        ) // ALLOCATION_BLOCK_SIZE
        blocks = free_blocks[block_offset:block_offset + extent_blocks]
        logical_extent = extent_index * 2

        entry = bytearray(DIRECTORY_ENTRY_SIZE)
        entry[0] = 0
        entry[1:9] = name.ljust(8).encode("ascii")
        entry[9:12] = suffix.ljust(3).encode("ascii")
        if extent_records > LOGICAL_EXTENT_RECORDS:
            entry[12] = (logical_extent & 0x1F) + 1
            entry[15] = extent_records - LOGICAL_EXTENT_RECORDS
        else:
            entry[12] = logical_extent & 0x1F
            entry[15] = extent_records
        entry[14] = (logical_extent >> 5) & 0x3F
        for index, block in enumerate(blocks):
            entry[16 + index * 2:18 + index * 2] = block.to_bytes(2, "little")
            target = DIRECTORY_OFFSET + block * ALLOCATION_BLOCK_SIZE
            count = min(ALLOCATION_BLOCK_SIZE, extent_bytes)
            image[target:target + count] = padded[
                payload_offset:payload_offset + count
            ]
            if count < ALLOCATION_BLOCK_SIZE:
                image[target + count:target + ALLOCATION_BLOCK_SIZE] = bytes(
                    ALLOCATION_BLOCK_SIZE - count
                )
            payload_offset += count
            extent_bytes -= count

        directory_entry = free_entries[extent_index]
        target = DIRECTORY_OFFSET + directory_entry * DIRECTORY_ENTRY_SIZE
        image[target:target + DIRECTORY_ENTRY_SIZE] = entry
        used_blocks.update(blocks)
        block_offset += extent_blocks
        remaining_records -= extent_records

    if payload_offset != len(padded) or remaining_records != 0:
        raise ValueError(f"failed to write all of {added.name}")


def build_image(
    source: Path,
    output: Path,
    added_files: list[AddedFile] | None = None,
) -> None:
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

    additions = added_files or []
    for added in additions:
        if added.name in kept_names:
            raise ValueError(f"file already retained from source: {added.name}")
        add_user_zero_file(image, added, kept_blocks)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    print(
        f"created {output}: kept {len(kept_names)} user-0 files, "
        f"added {len(additions)}, removed {removed_files} files, "
        f"retained {len(kept_blocks)} data blocks"
    )


def added_file(argument: str) -> AddedFile:
    if "=" not in argument:
        raise argparse.ArgumentTypeError("expected CP/M-NAME=HOST-PATH")
    name, path = argument.split("=", 1)
    try:
        normalized = ".".join(part for part in split_cpm_filename(name) if part)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    host_path = Path(path)
    if not host_path.is_file():
        raise argparse.ArgumentTypeError(f"file does not exist: {host_path}")
    return AddedFile(normalized, host_path)


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
    parser.add_argument(
        "--add",
        action="append",
        default=[],
        type=added_file,
        metavar="CP/M-NAME=HOST-PATH",
        help="add one explicit user-0 file after stripping the source image",
    )
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must be different files")
    build_image(args.source, args.output, args.add)


if __name__ == "__main__":
    main()

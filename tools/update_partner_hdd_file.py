#!/usr/bin/env python3
"""Replace a CP/M file without changing a Partner hard disk's extents.

Partner hard disks use EXM=1.  Their 4 KiB allocation blocks allow one
directory entry to describe two 16 KiB logical extents.  Preserving those
entries avoids disk tools which incorrectly expand a file into overlapping
EX=0 and EX=1 entries.
"""

from __future__ import annotations

import argparse
from pathlib import Path


SECTOR_SIZE = 256
SECTORS_PER_TRACK = 32
BOOT_TRACKS = 1
DIRECTORY_ENTRIES = 1024
BLOCK_SIZE = 4096
EXTENT_SIZE = 128 * 128
EXTENT_MASK = 1


def cpm_name(entry: bytes) -> str:
    name = "".join(chr(value & 0x7F) for value in entry[1:9]).rstrip()
    suffix = "".join(chr(value & 0x7F) for value in entry[9:12]).rstrip()
    return f"{name}.{suffix}" if suffix else name


def replace_file(image_path: Path, payload_path: Path, filename: str) -> None:
    image = bytearray(image_path.read_bytes())
    payload = payload_path.read_bytes()
    directory_offset = BOOT_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE
    extents: list[tuple[int, int, list[int]]] = []

    for index in range(DIRECTORY_ENTRIES):
        offset = directory_offset + index * 32
        entry = image[offset:offset + 32]
        if len(entry) != 32 or entry[0] != 0 or cpm_name(entry) != filename:
            continue
        raw_extent = entry[12]
        logical_extent = (
            (raw_extent & ~EXTENT_MASK) | ((entry[14] & 0x3F) << 5)
        )
        record_count = (raw_extent & EXTENT_MASK) * 128 + entry[15]
        blocks = [
            int.from_bytes(entry[position:position + 2], "little")
            for position in range(16, 32, 2)
            if int.from_bytes(entry[position:position + 2], "little") != 0
        ]
        extents.append((logical_extent, record_count * 128, blocks))

    extents.sort(key=lambda item: item[0])
    if not extents:
        raise ValueError(f"CP/M user 0 file {filename} is missing")

    expected_extent = 0
    logical_size = 0
    for logical_extent, byte_count, blocks in extents:
        if logical_extent != expected_extent:
            raise ValueError(
                f"invalid or overlapping extent {logical_extent}; "
                f"expected {expected_extent}"
            )
        if byte_count > len(blocks) * BLOCK_SIZE:
            raise ValueError(f"extent {logical_extent} has too few blocks")
        logical_size += byte_count
        expected_extent += (byte_count + EXTENT_SIZE - 1) // EXTENT_SIZE

    if len(payload) > logical_size or logical_size - len(payload) >= 128:
        raise ValueError(
            f"{payload_path} is {len(payload)} bytes but the existing CP/M "
            f"record allocation represents {logical_size} bytes"
        )

    replacement = payload + bytes([0x1A]) * (logical_size - len(payload))
    source_offset = 0
    for _, byte_count, blocks in extents:
        remaining = byte_count
        for block in blocks:
            if remaining == 0:
                break
            count = min(BLOCK_SIZE, remaining)
            target_offset = directory_offset + block * BLOCK_SIZE
            image[target_offset:target_offset + count] = replacement[
                source_offset:source_offset + count
            ]
            source_offset += count
            remaining -= count
        if remaining != 0:
            raise ValueError("truncated allocation while writing replacement")

    if source_offset != len(replacement):
        raise ValueError("not all replacement bytes were written")
    image_path.write_bytes(image)
    print(
        f"updated {filename} in {image_path}: {len(payload)} bytes, "
        f"{logical_size} bytes including CP/M record padding"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("--name", default="PAKET.COM")
    args = parser.parse_args()
    replace_file(args.image, args.payload, args.name.upper())


if __name__ == "__main__":
    main()

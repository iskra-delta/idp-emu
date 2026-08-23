#!/usr/bin/env python3
"""Generate the packaged Partner CMOS seed.

The GDP BIOS reads the terminal mode and language from NVRAM register 0xAB.
Zero in the high nibble selects ANSI, which is what the emulator's GDP
terminal exposes; language 8 selects the Yugoslav character set. Register
0xAF's low nibble is the NVRAM checksum.
"""

from __future__ import annotations

import argparse
from pathlib import Path


CMOS_SIZE = 8
SEED_WITHOUT_CHECKSUM = bytes.fromhex("00 00 80 08 40 00 90 40")
EXPECTED_SEED = bytes.fromhex("00 00 80 08 40 00 90 4f")


def stamp_checksum(seed: bytes) -> bytes:
    if len(seed) != CMOS_SIZE:
        raise ValueError(f"Partner CMOS must contain exactly {CMOS_SIZE} bytes")

    result = bytearray(seed)
    result[7] &= 0xF0
    nibble_sum = sum((byte >> 4) + (byte & 0x0F) for byte in result) & 0x0F
    result[7] |= (-nibble_sum) & 0x0F
    return bytes(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "partner_cmos.bin",
        help="output CMOS file (default: repository partner_cmos.bin)",
    )
    args = parser.parse_args()

    seed = stamp_checksum(SEED_WITHOUT_CHECKSUM)
    if seed != EXPECTED_SEED:
        raise RuntimeError(f"unexpected CMOS seed: {seed.hex(' ')}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(seed)
    print(f"wrote {args.output}: {seed.hex(' ')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

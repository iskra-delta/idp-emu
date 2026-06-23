#!/usr/bin/env python3
#
# makecom.py
#
# Build one PartOS .COM file:
#   - either by wrapping an existing XL image with a small COM header
#   - or by synthesizing the XL image from a flat payload + relocation markers
#
# Raw-payload mode is useful for the first hand-written shell: the payload is
# linked at address 0, every relocation site is marked by a symbol whose name
# starts with "__reloc_", and this tool turns those markers into the XL
# relocation table automatically before it adds the COM wrapper.
#
# 2026-06-22   tstih
import argparse
import pathlib
import re
import struct
import sys

XL_HDR_SIZE = 12
COM_HDR_SIZE = 16
RELOC_PREFIX = "__reloc_"


def read_file(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if not data:
        raise ValueError(f"empty input: {path}")
    return data


def parse_map_symbols(path: pathlib.Path) -> dict[str, int]:
    text = path.read_text(encoding="ascii", errors="replace")
    pat = re.compile(r"([0-9A-Fa-f]{8})\s+([_A-Za-z.][_A-Za-z0-9$.]*)")
    out: dict[str, int] = {}
    for m in pat.finditer(text):
        out.setdefault(m.group(2), int(m.group(1), 16))
    if not out:
        raise ValueError(f"no symbols found in {path}")
    return out


def build_xl_from_payload(payload: bytes, symbols: dict[str, int], entry_symbol: str) -> bytes:
    if entry_symbol not in symbols:
        raise ValueError(f"entry symbol '{entry_symbol}' not found in map")
    entry = symbols[entry_symbol]
    if not (0 <= entry < len(payload)):
        raise ValueError(
            f"entry symbol '{entry_symbol}' = 0x{entry:04X} is outside "
            f"payload size {len(payload)}"
        )

    reloc_sites = sorted(
        (addr, name)
        for name, addr in symbols.items()
        if name.startswith(RELOC_PREFIX)
    )
    reloc_table = bytearray()
    seen: set[int] = set()
    for offset, name in reloc_sites:
        if offset in seen:
            continue
        seen.add(offset)
        if offset < 0 or offset + 1 >= len(payload):
            raise ValueError(
                f"relocation marker {name} = 0x{offset:04X} falls outside "
                f"payload size {len(payload)}"
            )
        reloc_table += struct.pack("<HBB", offset, 2, 0)

    hdr = bytearray(XL_HDR_SIZE)
    hdr[0:2] = b"XL"
    hdr[2] = 0x01
    hdr[3] = 0x00
    struct.pack_into("<H", hdr, 4, entry)
    struct.pack_into("<H", hdr, 6, len(payload))
    struct.pack_into("<H", hdr, 8, len(seen))
    struct.pack_into("<H", hdr, 10, 0)
    return bytes(hdr + reloc_table + payload)


def validate_xl(xl: bytes) -> int:
    if len(xl) < XL_HDR_SIZE:
        raise ValueError("XL image is smaller than its fixed header")
    if xl[0:2] != b"XL":
        raise ValueError("XL image does not start with magic 'XL'")
    if xl[2] != 0x01:
        raise ValueError(f"unsupported XL version {xl[2]}")
    entry = struct.unpack_from("<H", xl, 4)[0]
    code_size = struct.unpack_from("<H", xl, 6)[0]
    reloc_count = struct.unpack_from("<H", xl, 8)[0]
    need = XL_HDR_SIZE + reloc_count * 4 + code_size
    if need > len(xl):
        raise ValueError(
            f"XL image truncated: need {need} bytes, only have {len(xl)}"
        )
    return entry


def wrap_com(xl: bytes, stack_size: int, align: int) -> bytes:
    if not (0 < stack_size <= 0xFFFF):
        raise ValueError("stack size must be in 1..65535")
    if not (align >= 1):
        raise ValueError("alignment must be >= 1")

    entry = validate_xl(xl)

    hdr = bytearray(COM_HDR_SIZE)
    hdr[0:2] = b"CM"
    hdr[2] = 0x01
    hdr[3] = 0x00
    struct.pack_into("<H", hdr, 4, stack_size)
    struct.pack_into("<H", hdr, 6, entry)
    struct.pack_into("<H", hdr, 8, COM_HDR_SIZE)
    struct.pack_into("<H", hdr, 10, len(xl))
    struct.pack_into("<H", hdr, 12, 0)
    struct.pack_into("<H", hdr, 14, 0)

    com = bytes(hdr) + xl
    if align > 1:
        pad = (-len(com)) % align
        if pad:
            com += bytes(pad)
    return com


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a PartOS .COM wrapper")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--input-xl", type=pathlib.Path, help="existing XL image")
    mode.add_argument("--payload", type=pathlib.Path, help="flat payload binary linked at 0")
    parser.add_argument("--map", type=pathlib.Path, help="payload linker map (raw mode)")
    parser.add_argument(
        "--entry-symbol",
        default="shell_entry",
        help="payload entry symbol in raw mode (default: shell_entry)",
    )
    parser.add_argument(
        "--output-xl",
        type=pathlib.Path,
        help="optional path to write the synthesized/final XL image",
    )
    parser.add_argument("--stack", type=int, required=True, help="expected stack size")
    parser.add_argument(
        "--align",
        type=int,
        default=256,
        help="pad final COM size to this multiple (default: 256)",
    )
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True, help="output .COM path")
    args = parser.parse_args()

    try:
        if args.input_xl is not None:
            xl = read_file(args.input_xl)
            validate_xl(xl)
        else:
            if args.map is None:
                raise ValueError("--map is required with --payload")
            payload = read_file(args.payload)
            symbols = parse_map_symbols(args.map)
            xl = build_xl_from_payload(payload, symbols, args.entry_symbol)

        if args.output_xl is not None:
            args.output_xl.parent.mkdir(parents=True, exist_ok=True)
            args.output_xl.write_bytes(xl)

        com = wrap_com(xl, args.stack, args.align)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(com)
        return 0
    except Exception as exc:  # noqa: BLE001 - compact CLI tool
        print(f"makecom.py: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

# zx0 tool

This directory contains a small standalone ZX0 toolchain for this repository.

- `src/main.cpp` and `src/zx0.cpp` provide a host-side compressor written in modern lowercase C++.
- `src/dzx0_standard.s` provides the standard 68-byte Z80 decoder in SDASZ80 syntax.

The compressor keeps the official ZX0 v2 bitstream format so its output can be
decoded by the supplied Z80 routine.

## Build

```sh
make -C tools/zx0
```

This produces:

- `tools/zx0/bin/zx0`

To sanity-check the Z80 source with the same assembler used by PartOS:

```sh
make -C tools/zx0 asm-check
```

## Usage

```sh
tools/zx0/bin/zx0 [options] input [output.zx0]
```

Options:

- `-f`, `--force`: overwrite the output file
- `-c`, `--classic`: emit ZX0 classic v1 format
- `-b`, `--backwards`: compress for backwards decompression
- `-q`, `--quick`: use the faster non-optimal search mode
- `-s`, `--skip N`: skip the first `N` bytes while still allowing matches into them

## Notes

This implementation is based on the official ZX0 reference sources by Einar
Saukas and Urusergi. The assembly decoder is a syntax adaptation of the
official standard Z80 decoder.

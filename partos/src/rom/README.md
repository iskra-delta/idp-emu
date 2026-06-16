# PARTOS ROM

This directory holds the ROM firmware: the reset-time code, the decompressed
stage-1 boot code, the setup screen, and the tiny raw-media loader that
hands off to the operating-system image.

The authoritative documentation now lives in:

- `partos/docs/PARTOS-VOLUME-1-ROM.md`

Short version:

- one ROM source tree boots both the plain Partner and the GDP-capable model
- the physical ROM is `2 KiB`, so the firmware is split into a fixed stage 0
  plus a compressed stage 1
- stage 1 runs from RAM at `0x2000`
- setup is stored in the MM58167 NVRAM block with a 4-bit checksum
- boot order is currently `fd0` first and `sda` second
- the ROM reads sector `0` to `0xDF00`, sectors `1..32` to `0xE000..0xFFFF`,
  checks `0x55AA`, and jumps to the loaded image

If a behavior is ROM-only, this directory is where it belongs.

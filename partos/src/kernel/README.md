# kernel

This directory holds the early shared-memory kernel image that is loaded by
the ROM and linked together with the driver layer.

The authoritative documentation now lives in:

- `partos/docs/PARTOS-VOLUME-2-KERNEL.md`

Short version of the current state:

- `kernel.bin` is `6120` bytes
- `_CODE` is linked at `0xE818`
- `_HEAP` is linked at `0xFA00` (896 bytes)
- `_IM2` is linked at `0xFE00` (512 bytes, two pages)
- `_PAGE0` is linked at `0xF900`
- `__sys_page0_install` lives at fixed address `0xF96B`
- the current ROM caller jumps to `__sys_page0_install` with `HL = 0xE000`
  and `B = model byte`
- `__sys_kernel` exists, but it is not currently the direct continuation
  value supplied by the ROM

What the kernel already has:

- page-0 installation into both banks
- shared vector table
- interrupt reference counting
- heap and system-object primitives
- intrusive list helpers
- linked-in drivers for SIO, RTC, NVRAM, GDP, floppy, and SASI/Xebec disk

What it still does not have:

- a full early-init sequence after handoff
- a finished OS-image contract for the reserved sectors
- the higher-level operating-system layer that will eventually live in
  `src/os/`

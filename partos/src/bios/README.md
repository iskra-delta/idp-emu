# bios

This directory is the ROM side of PartOS.

- `bootstrap.s` and `dzx0_standard.s` form the fixed ROM-resident stage-0 loader.
- `start.s` is the compressed stage-1 BIOS image that is inflated to RAM at `0x0800`.
- `bios.s` is the early ROM-side setup-screen entry used when `SETUP` is pressed during boot.
- `hd.s` and `hd.inc` contain ROM-side hard-disk type tables derived from NVRAM selectors.
- `model.s` contains minimal ROM-side machine-model detection.
- `kbd.s` contains direct keyboard polling on SIO0A.
- `avdc.s` contains minimal GDP/AVDC text-mode output helpers.
- `print.s` and `print.inc` contain the ROM-side `print_at` dispatcher.
- `nvram.s` contains direct 8-byte NVRAM read/write/checksum helpers.
- Additional ROM-only BIOS code should live here as the firmware is refined.

The intended split is:

- `src/bios/` = ROM firmware
- `src/kernel/` = microkernel
- `src/drivers/` = device support
- `os/` = operating-system software and programs

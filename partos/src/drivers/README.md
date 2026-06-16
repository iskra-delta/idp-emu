# drivers

This directory contains device-support code.

It is the hardware-facing layer for modules such as:

- serial I/O
- RTC/NVRAM
- GDP
- floppy disks
- hard disks

The intended split is:

- `src/bios/` = ROM firmware
- `src/kernel/` = microkernel
- `src/drivers/` = device support
- `os/` = operating-system software and programs

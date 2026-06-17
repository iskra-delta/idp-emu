# os

This directory is reserved for the higher-level operating-system layer: the
part above the kernel and drivers.

The authoritative documentation now lives in:

- `partos/docs/PARTOS-VOLUME-3-OS.md`

That higher-level OS software is still in its first steps. The concrete pieces
now living here are:

- `fat.s`, `fat_mount.s`, `fat_lookup.s`, `fat_file.s`, `fat_create.s`,
  `fat_mutate.s` plus `fat.inc`: a small async FAT12/FAT16 service built on
  top of the kernel event and block-device interfaces
- `process.s` plus `process.inc`: the current process object and XL-image
  loader built on top of kernel threads, events and ownership
- `syscall.s`: the current empty `"yos"` syscall service registration
- `boot.s`: the first OS bootstrap thread that mounts the boot volume and
  launches `/SHELL.XL`

This first cut is intentionally narrow:

- queue async mount and lookup requests as sysobj-shaped nodes,
- run one dedicated FAT worker thread,
- parse superfloppy or MBR-backed FAT12/FAT16 boot metadata,
- fill `fat_fs_t` and perform DOS 8.3 path lookup,
- open or create regular files asynchronously,
- transfer 256-byte blocks and grow files by cluster when needed.

The current completion/minimization plan for this FAT work lives in:

- `partos/docs/FAT-MINIMIZATION-PLAN.md`

The real bootable binaries today are still the ROM and the early kernel.

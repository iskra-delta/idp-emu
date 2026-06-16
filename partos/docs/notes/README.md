# PartOS Notes Index

Categorized working notes. Each file carries its category and date(s) in
the header; new findings are merged into the matching file (adding the
date), or start a new dated file if no category fits. Notes from the old
PartOS repository (`~/data/iskra-delta/partos/docs/notes/`, dated lowercase
files) were merged here on 2026-06-11.

## Architecture & planning

- [DECISIONS.md](DECISIONS.md) — design decisions log + rationale
- [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) — living checklist of unresolved items
- [MEMORY-LAYOUT.md](MEMORY-LAYOUT.md) — 2024 memory layout plan (historical; superseded by ARCHITECTURE.md, kept for the IM2 table idea and budgets)

## OS design

- [UKERNEL.md](UKERNEL.md) — microkernel services, syscall table, DOS-compatible codes, locking/multitasking references
- [FILE-SYSTEM.md](FILE-SYSTEM.md) — FAT12/16 considerations, BPB boot sector, DOS-diskette compatibility
- [BIOS-GUI.md](BIOS-GUI.md) — BIOS startup screen mockup, bare device naming scheme

## Boot & drivers

- [FLOPPY-BOOT.md](FLOPPY-BOOT.md) — minimal i8272+DMA floppy boot sequence (working asm)
- [HARDDRIVE-BOOT.md](HARDDRIVE-BOOT.md) — minimal SASI/Xebec boot sequence (working asm)

## Hardware reference

- [MEMORY-BANKING.md](MEMORY-BANKING.md) — banking/ROM-overlay hardware quirks
- [IO-MAP.md](IO-MAP.md) — Partner I/O map, interrupt topology, FDC/SASI details

## Tooling

- [EMULATOR-SUPPORT.md](EMULATOR-SUPPORT.md) — idp-emu capabilities and TODOs for PartOS work

The architecture itself is documented in
[../partos/ARCHITECTURE.md](../partos/ARCHITECTURE.md).

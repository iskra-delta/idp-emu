# Note: Open Questions

Category: Planning / Open Questions
Date(s): 2026-06-11

Living checklist. Tick items off (and record the outcome in
[DECISIONS.md](DECISIONS.md)) as they are resolved.

- [ ] Driver format specification (user to provide) — defines how the disk
      drivers present themselves to the OS.
- [ ] Interrupt mode choice: IM2 throughout (vector table in common RAM,
      I register pointing at it) vs IM1 for simplicity.
- [ ] Page-0 layout: which RSTs are BIOS entry points vs. reserved for the
      OS; what else lives in 0x0000-0x00FF (BIOS version, magic, pointers
      to the BIOS jump table?).
- [ ] BIOS proper layout at the top of common RAM: code/data/stack
      boundaries (fixed once sizes are known); how much common RAM the
      BIOS may claim vs. the OS.
- [ ] BIOS call convention: RST-based, jump-table-based, or both; register
      usage, error reporting.
- [ ] One ROM for both machine models (CRT and GDP) with runtime detection,
      or separate ROM builds?
- [ ] Toolchain: xyz `xlink`-based build vs. plain assembler; how
      `partos/build/` produces the exactly-2048-byte image in `partos/bin/`.
- [ ] Per-bank usage policy for 0x0100-0xBFFF (what lives in bank 1 vs
      bank 2).
- [ ] If the BIOS is disk-loaded (doesn't fit ROM): on-disk location and
      format of the BIOS image (reserved tracks? fixed LBA range?).

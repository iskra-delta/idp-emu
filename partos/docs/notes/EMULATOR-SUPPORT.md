# Note: Emulator-Side Support for PartOS Development

Category: Tooling / Emulator (idp-emu)
Date(s): 2026-06-11

What the idp-emu emulator provides for PartOS bring-up, and what still
needs work on the emulator side.

## Available Now

- The emulator runs ROMs **unpatched** (all ROM workarounds were removed
  in June 2026), so PartOS behaves exactly as written.
- `--rom` loads the image; `--fd0/--fd1` mount floppies, `--hdd` mounts the
  Xebec/SASI hard disk image.
- **xdbg remote debugger**: run/pause/step/breakpoints/memory over TCP
  (started from the emulator GUI), for source-level ROM debugging.
- **Headless probes** (`idp-probe`, `idp-gdp-probe`): scriptable boot
  tracing without the GUI — useful for automated PartOS boot tests.

## Emulator TODOs

- Disk **write** path: the emulator currently wires only read callbacks
  for the i8272 and the S1410. PartOS disk drivers will need writes;
  extend `i8272.h` / `s1410.h` and `partner.cpp` accordingly.
- An image shorter than 2048 bytes is rejected by the ROM loader — the
  PartOS build must pad the binary to exactly 2 KB.

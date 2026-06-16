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
- Disk images are now writable: the emulator persists i8272 sector writes,
  `FORMAT TRACK`, and S1410/SASI block writes back to the mounted image files.
- **udap DAP debugger** (https://github.com/retro-vault/udap):
  run/pause/step/breakpoints/memory/disassembly over TCP, with SDCC
  CDB/MAP source-level debugging from VSCode. Start with `--dap 4711`
  or from the emulator GUI (Machine -> Debugger), then connect a
  `"type": "udap"` launch configuration with `"debugServer": 4711`.
- **Headless probes** (`idp-probe`, `idp-gdp-probe`): scriptable boot
  tracing without the GUI — useful for automated PartOS boot tests.

## Emulator TODOs

- Raw floppy images still abstract away gap bytes and true physical interleave;
  `FORMAT TRACK` updates sector contents/IDs at the logical CHRN level.
- An image shorter than 2048 bytes is rejected by the ROM loader — the
  PartOS build must pad the binary to exactly 2 KB.

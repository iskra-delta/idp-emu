# Pattern: CTC/PIO Glue Signals and Boot Dependencies

Category: Z80 CTC / Z80 PIO / Board Glue  
Date(s): 2026-03-20

## Problem / Purpose

Record board-level glue behavior that affects motor timing, display-chip
gating, and interrupt routing.

## Findings / Observations

- CTC programming appears in both CRT and GDP startup paths and is part of
  timing/interrupt setup, not optional noise.
- GDP local PIO (`0x30..0x33`) controls video-side enables/mode lines used to gate EF9367 vs AVDC behavior.
- In GDP traces and schematics context:
  - PIO outputs include lines associated with banking/mode and video interrupt glue.
  - Display chips can be functionally “present but gated” depending on PIO configuration.

## Emulation Rules (Practical)

- Treat PIO outputs as active control lines, not passive debug registers.
- Preserve CTC initialization order because ROM may depend on it before storage/display routines.
- Keep glue behavior deterministic:
  - If a path depends on a gate line, represent it explicitly.
  - Avoid hidden bypasses that make one model boot while breaking another.

## Current Status

- GDP board PIO window is mapped and participating in chip enable/gating logic.
- Further refinement is expected as AVDC/EF behavior is completed.

## See Also

- `docs/notes/patterns/SIO-PIO-TCP-VIRTUAL-DEVICES.md` (main-system PIO/SIO virtual-device behavior and TCP serial redirection)

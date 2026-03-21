# Pattern: SASI/Xebec S1410 Handshake

Category: Xebec S1410 / SASI / HDD Boot  
Date(s): 2026-03-20

## Problem / Purpose

Capture ROM-level SASI expectations to debug HDD boot loops around
`IN A,(10h)` and request/BSY phases.

## Findings / Observations

- CRT ROM HDD path frequently polls port `0x10` (status/control handshake) and uses:
  - `0x11` for data
  - `0x12` for reset/control
- Common loops:
  - waiting for BSY assertion after select
  - waiting for REQ transitions before byte exchange
- If REQ/BSY timing is wrong, firmware stalls at loops around:
  - `0x05C2` (BSY wait)
  - `0x05CE` (REQ wait)

## Emulation Rules (Practical)

- Session/select lifecycle should match SASI sequencing:
  1. assert select
  2. target asserts BSY
  3. REQ-driven command/data byte transfers
  4. status/response completion
- Keep the controller state machine explicit (`idle`, `command`, `data`,
  `status`) and derive status bits directly from state.
- Avoid forcing “always-ready” shortcuts that break retry logic in ROM.

## Current Status

- HDD path is partially functional for diagnostics but still under active bring-up versus GDP/CP/M goals.
- Floppy path should remain stable while HDD logic evolves.

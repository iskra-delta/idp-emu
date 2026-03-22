# Pattern: SIO Keyboard Input Path

Category: Z80 SIO / Keyboard / Boot Input  
Date(s): 2026-03-20

## Problem / Purpose

Document how Partner ROM expects keyboard bytes during boot and why loops at
`0x009F` are normal until RX-ready is asserted.

## Findings / Observations

- CRT ROM `wait_key` sequence:
  - `0x009F: IN A,(D9h)` status
  - `0x00A1: BIT 0,A` (RX-ready)
  - `0x00A3: JR Z,009Fh`
  - `0x00A5: IN A,(D8h)` read byte
- This is expected busy-poll behavior, not a failure by itself.
- Valid boot key flow:
  - `'F'` goes to floppy path
  - `'A'` goes to HDD path
- GDP firmware may still emit bootstrap text over SIO while display chips are
  initializing; this is useful as a diagnostic side channel.

## Emulation Rules (Practical)

- RX-ready bit must only assert when a byte is actually queued.
- Reading data should consume the byte and clear RX-ready according to SIO behavior.
- TX-ready polling must be honored; ROM prints rely on this.
- Keyboard injection should target the active channel used by ROM (typically channel A during bootstrap).

## Current Status

- SIO polling/input path works for boot selection and text diagnostics.
- Remaining GDP issues are not caused by basic SIO key ingress.

## See Also

- `docs/notes/patterns/SIO-PIO-TCP-VIRTUAL-DEVICES.md` (current virtual-device routing, mouse protocols, and TCP bridge behavior)

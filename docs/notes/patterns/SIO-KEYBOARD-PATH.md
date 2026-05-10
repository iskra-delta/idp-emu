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

## GDP Keyboard Local Codes

The running GDP software contains its own keyboard ISR at `0xFEA7` which reads
raw bytes from SIO A data port `0xD8` and handles several codes before they are
placed into the normal key queue.

Observed behavior from the ISR:

- `0xB0`
  - handled immediately by the ISR
  - toggles bit 0 of RAM flag byte `0xFF19`
  - this is the GDP keyboard's `SET UP` local-function code
- `0xFE`
  - handled immediately by the ISR
  - toggles bit 1 of `0xFF19`
  - this is the GDP keyboard's `NO SCROLL` local-function code
- `0xEA`
  - handled immediately by the ISR
  - clears `0xFF19`
  - also clears `0xF9D4` and vectors through `0xFA03`
- `0xE0`
  - consumed by the ISR and ignored
- `0xFF`
  - consumed by the ISR and ignored

Implication for emulation:

- `SET UP` is not an `ESC` sequence on the Partner GDP keyboard path.
- `NO SCROLL` is not an `ESC` sequence either.
- These local keyboard functions must be injected as raw keyboard bytes through
  SIO A so the GDP ISR can process them.
- `ESC`-prefixed sequences for cursor/application keys, when used, are a
  higher-level convention consumed later by software and should not be confused
  with the GDP keyboard's local-function codes.

## See Also

- `docs/notes/patterns/SIO-PIO-TCP-VIRTUAL-DEVICES.md` (current virtual-device routing, mouse protocols, and TCP bridge behavior)

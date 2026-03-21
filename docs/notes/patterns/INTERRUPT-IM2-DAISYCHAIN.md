# Pattern: IM2 Interrupt and Daisy-Chain Behavior

Category: Z80 / IM2 / Interrupt Glue  
Date(s): 2026-03-20

## Problem / Purpose

Capture interrupt behavior that repeatedly affects Partner bring-up (`HALT`
waits, repeated ISR entry, and pending-interrupt clearing).

## Findings / Observations

- Partner ROM enables **IM2** early and relies on vector page `I=0x02` during initial boot.
- In CRT ROM flow (`rom-crt-anno.txt`):
  - vector table base is around `0x0218`
  - `0x18 -> 0x03EB` (FDC ISR: `EI; SCF; RETI`)
  - `0x1A -> 0x0509` (HDD ISR path)
  - `0x1C -> 0x0445` (CTC/SIO ISR path)
- Boot logic uses `EI` + `HALT` as synchronization points; progress depends on
  device IRQ edges being latched and then correctly cleared after ISR/result
  handling.
- If pending interrupt state is never cleared (or is reasserted immediately),
  the CPU can loop around ISR entry/return.
- If an interrupt never arrives, the CPU remains parked in `HALT` (common at
  seek/read waits).

## Emulation Rules (Practical)

- IM2 acknowledge must fetch vector from the active page and call the mapped handler.
- Device-side “pending” flags must be cleared at the same semantic moment as real hardware:
  - FDC: after proper sense/result phase transitions
  - CTC/SIO/PIO: after their IRQ service/ack flow
- Avoid “sticky IRQ” unless confirmed by hardware behavior.

## Current Status

- Core IM2 routing is active.
- Remaining failures in GDP path are now mostly display-path related, not basic IM2 routing.

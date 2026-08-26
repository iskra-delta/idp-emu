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
- The motherboard schematic establishes this priority: CTC, DMA, SIO #1,
  SIO #2, motherboard PIO, discrete FDC request/in-service glue, then the
  expansion IEI/IEO connection. The GDP-local PIO is the final device on a
  Partner G.

## Emulation Rules (Practical)

- IM2 acknowledge must fetch vector from the active page and call the mapped handler.
- Device-side “pending” flags must be cleared at the same semantic moment as real hardware:
  - FDC: its edge-latched request is acknowledged with the `0xE8..0xEF`
    vector latch, remains in service, and is released by RETI
  - CTC/SIO/PIO: after their IRQ service/ack flow
- A pending DMA interrupt must continue to hold `/INT` until acknowledge.
- New PIO/SIO requests that arise during M1 are deferred until M1 ends.
- Avoid “sticky IRQ” unless confirmed by hardware behavior.

## Current Status

- Core IM2 routing, full board priority, FDC acknowledge/RETI state, DMA
  request hold, and PIO/SIO M1 deferral have focused regression tests.

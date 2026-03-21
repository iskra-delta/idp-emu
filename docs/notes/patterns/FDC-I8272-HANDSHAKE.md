# Pattern: i8272 FDC Command/Result Handshake

Category: Intel i8272 / FDC / DMA  
Date(s): 2026-03-20

## Problem / Purpose

Track exact ROM expectations for FDC status bits and command/result timing to
avoid loops at `0x0259`/`0x0266` and HALT stalls.

## Findings / Observations

- ROM helper (`send_fdc_cmd`):
  - loops on `IN (F0h)` until `(MSR & 0xC0) == 0x80` (RQM=1, DIO=0)
  - then writes command/data to `F1h`
- ROM helper (`read_fdc_result`):
  - loops until `(MSR & 0xC0) == 0xC0` (RQM=1, DIO=1)
  - then reads from `F1h`
- Boot path uses:
  - reset/sense
  - specify
  - seek/recalibrate with `EI; HALT` wait
  - read-data + result-phase parsing

## Emulation Rules (Practical)

- Do not expose result phase bits too early.
- Transition from command to result phase only when command lifecycle is complete (including IRQ behavior expected by ROM).
- Clear pending interrupt states when ROM performs SENSE/RESULT consumption.
- Keep motor/status (`0x98`) behavior coherent with CTC-driven control logic.

## Current Status

- CRT floppy boot path reaches CP/M in current baseline.
- GDP path still needs display-side completion, but the FDC handshake rules
  above remain required and must not regress.

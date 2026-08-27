# Pattern: CTC/PIO glue signals and boot dependencies

Category: Z80 CTC / Z80 PIO / Board Glue  
Date(s): 2026-03-20, 2026-08-26

## Problem / Purpose

Record board-level glue behavior that affects motor timing, display-chip
gating, and interrupt routing.

## Findings / Observations

- CTC programming appears in both CRT and GDP startup paths and is part of
  timing/interrupt setup, not optional noise.
- Motherboard CTC E67 pin 23 (`CLK/TRG0`) receives MC14411 F13 net `XX1`, a
  1600 Hz square wave. E67 pin 7 (`ZC/TO0`) and pin 22 (`CLK/TRG1`) share
  `_NET_167`; channel 0 therefore clocks channel 1 in real copper, without a
  PAL or software-controlled gate.
- E67 pin 8 (`ZC/TO1`) is `XX2`. E91 (74LS32) ORs it with `RESET+`, E94
  (74LS04) inverts the result, and E107 pin 13 asynchronously clears the
  motor-control flip-flop. The timeout is a pulse, not a persistent CTC level.
- E67 pin 21 (`CLK/TRG2`) is unconnected. E67 pin 20 (`CLK/TRG3`) reaches JJ10
  pin 1; on the GDP configuration this optional path receives conditioned
  `AVDINT-`.
- GDP local PIO (`0x30..0x33`) controls video-side enables/mode lines used to gate EF9367 vs AVDC behavior.
- In GDP traces and schematics context:
  - PIO outputs include lines associated with banking/mode and video interrupt glue.
  - Display chips can be functionally “present but gated” depending on PIO configuration.

## Emulation Rules (Practical)

- Treat PIO outputs as active control lines, not passive debug registers.
- Preserve CTC initialization order because ROM may depend on it before storage/display routines.
- Count only the selected physical transition. Changing the slope bit is not
  itself a trigger edge.
- Interpret a zero time constant as 256. In timer mode, apply the selected /16
  or /256 prescaler. In counter mode, synchronise an external transition to
  the system clock and decrement once.
- If firmware writes a replacement control word and constant while a channel
  is running, finish the current down-count first and load the replacement at
  zero. ZC/TO0..2 are active-high one-clock pulses; channel 3 has no ZC/TO pin.
- Use IM2 vectors `(base & f8h) + 2 * channel`, preserve channel priority, and
  block lower daisy-chain devices until RETI releases the in-service channel.
- Keep glue behavior deterministic:
  - If a path depends on a gate line, represent it explicitly.
  - Avoid hidden bypasses that make one model boot while breaking another.

## Current Status

- GDP board PIO window is mapped and participating in chip enable/gating logic.
- CTC timing, counter edges, delayed reprogramming, interrupt daisy behavior,
  and the Partner channel-0/channel-1 motor cascade have focused regressions.

## Reference programs

- The [Zilog Z80 CPU Peripherals User Manual](https://www.zilog.com/docs/z80/um0081.pdf)
  defines the edge synchronisation, timer start delay, time-constant reload,
  ZC/TO pulse, and daisy-chain rules used by the tests.
- A public [ZX Spectrum Next CTC example](https://gist.github.com/taylorza/5e0cd21acaba43e5369bb5270ed29d33)
  uses the standard `a5h` control word followed by time constant 249 and an
  IM2 handler. The Next has extra interrupt routing, but the two-byte CTC
  programming sequence is the same Z80 CTC format exercised here.

## See Also

- `docs/notes/patterns/SIO-PIO-TCP-VIRTUAL-DEVICES.md` (main-system PIO/SIO virtual-device behavior and TCP serial redirection)
- `docs/notes/patterns/DMA-BUS-READY.md`

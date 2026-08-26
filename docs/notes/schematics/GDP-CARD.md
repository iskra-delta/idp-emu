# Partner G GDP card

Canonical source: [iskra-delta/PartnerGDP](https://github.com/iskra-delta/PartnerGDP)

Pinned revision: [`140c852cf9238c5dc868f3c959167f4083d93dbc`](https://github.com/iskra-delta/PartnerGDP/tree/140c852cf9238c5dc868f3c959167f4083d93dbc),
committed 2026-08-23.

## KiCad entry point and sheets

Open
[`gdp/gdp.kicad_pro`](https://github.com/iskra-delta/PartnerGDP/blob/140c852cf9238c5dc868f3c959167f4083d93dbc/gdp/gdp.kicad_pro).
The root `gdp.kicad_sch` references the numbered hierarchical sheets
`1.kicad_sch` through `15.kicad_sch`. Keep `GDP.kicad_sym`, `sym-lib-table`,
and the numbered sheets together. The full pinned source directory is
[`gdp/`](https://github.com/iskra-delta/PartnerGDP/tree/140c852cf9238c5dc868f3c959167f4083d93dbc/gdp).

The repository also contains an annotated source drawing,
[`doc/gdp_schema_annotated.pdf`](https://github.com/iskra-delta/PartnerGDP/blob/140c852cf9238c5dc868f3c959167f4083d93dbc/doc/gdp_schema_annotated.pdf),
component placement, validation netlists, photographs, board-difference notes,
and the documented HCK and video-stabilization modifications.

## PAL coverage

The pinned KiCad/validation material identifies these programmable devices and
their board connectivity:

| Reference | Device |
| --- | --- |
| IC7 | PAL16L8 |
| IC12 | PAL16R4 |
| IC22 | PAL16L8 |
| IC24 | PAL10L8 |
| IC49 | PAL16R4 |

Sheet `3.kicad_sch` contains IC7, IC22, IC24, and IC49. Sheet
`12.kicad_sch` contains IC12. No standalone `.jed`, `.pld`, or equation file
is present at the pinned revision, so preserve the decoded signal names and
pin/net connectivity from the KiCad sheets rather than assuming generic PAL
behavior.

## Emulator relevance

This is the primary board-level reference for the EF9367 graphics processor,
SCN2674 AVDC, SCB2675 CMAC, GDP-local Z80 PIO, graphics and text memories,
banking, XOR, format selection, clocks, synchronization, writable character
RAM, and board glue. Compare it with:

- [`GDP-AVDC-CMAC-TIMING.md`](../patterns/GDP-AVDC-CMAC-TIMING.md)
- [`GDP-PIO.md`](../patterns/GDP-PIO.md)
- [`GDP-PROGRAMMING.md`](../patterns/GDP-PROGRAMMING.md)
- [`AVDC-UDG.md`](../patterns/AVDC-UDG.md)

## Connections verified against the emulator

- EF9367 `/IRQ` drives GDP PIO `ASTB`. Conditioned AVDC `/IRQ` drives `BSTB`
  and is also the source offered to the optional motherboard CTC channel 3
  connection. These signals are not PIO Port A bits 5 and 6.
- The GDP-local PIO continues the motherboard expansion daisy chain and is the
  lowest-priority fitted interrupt device.
- EF9367 `VB` is derived from the 525-line, 1.5 MHz raster timing. `MW`, light
  pen, `BLANK`, interrupt status, format, bank, XOR, and scroll signals now
  retain their distinct board-level roles.
- A line-style mask begins at the command origin. Consequently a negative-
  direction vector is spatially observed from its endpoint in the opposite
  order: `11001100` can appear as `00110011` when read left-to-right. The
  schematic supplies no direction-dependent mask inverter, so this follows
  traversal direction rather than a globally reversed pattern register.

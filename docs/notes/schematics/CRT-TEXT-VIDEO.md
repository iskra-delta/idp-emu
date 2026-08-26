# Partner P CRT text-video board

Canonical source:
[iskra-delta/IskraDeltaPartnerVideo](https://github.com/iskra-delta/IskraDeltaPartnerVideo)

Pinned revision: [`89c085b3157072d1eb21bbf89ac6397db43bdba4`](https://github.com/iskra-delta/IskraDeltaPartnerVideo/tree/89c085b3157072d1eb21bbf89ac6397db43bdba4),
committed 2023-08-23.

The repository identifies this as the reverse-engineered Partner text-video
board `30 797 044` used by the Partner P/CRT configuration.

## KiCad entry point and sheets

Open
[`partner_video/partner_video.kicad_pro`](https://github.com/iskra-delta/IskraDeltaPartnerVideo/blob/89c085b3157072d1eb21bbf89ac6397db43bdba4/partner_video/partner_video.kicad_pro).
The root sheet is `partner_video.kicad_sch`; its functional sheets are:

- `address_decoding.kicad_sch`
- `cpu.kicad_sch`
- `crtc.kicad_sch`
- `crtc_mods.kicad_sch`
- `dma.kicad_sch`
- `memory.kicad_sch`
- `pit_uart.kicad_sch`
- `powers.kicad_sch`
- `uart_mods.kicad_sch`

Browse the complete pinned
[`partner_video` directory](https://github.com/iskra-delta/IskraDeltaPartnerVideo/tree/89c085b3157072d1eb21bbf89ac6397db43bdba4/partner_video)
to keep the hierarchical sheets, custom symbol libraries, and
`sym-lib-table` together. A ready-to-read export is
[`partner_video_white.pdf`](https://github.com/iskra-delta/IskraDeltaPartnerVideo/blob/89c085b3157072d1eb21bbf89ac6397db43bdba4/partner_video/partner_video_white.pdf).

## Emulator relevance

This is the board-level source for the Partner P text subsystem: local 8085,
DMA, CRTC, PIT/UART, video memory, character storage, address decoding, and
documented board modifications. Do not apply this architecture to the Partner
G GDP card; the two machines reach text display through different hardware.

## Motherboard boundary

The motherboard CPU does not share the CRT card's local 8085 address, data, or
I/O bus and cannot program its CRTC or video RAM. Its usable interface is the
serial link through motherboard SIO #1 channel A. The emulator therefore keeps
the entire CRT card collapsed to a terminal abstraction while retaining the
motherboard SIO timing, interrupts, and modem/clock pins.

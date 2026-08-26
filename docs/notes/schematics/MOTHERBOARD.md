# Partner 40 motherboard

Canonical source: [iskra-delta/Partner40](https://github.com/iskra-delta/Partner40)

Pinned revision: [`75b28222770551c5fbbf8bfb3a2a273d7f73f817`](https://github.com/iskra-delta/Partner40/tree/75b28222770551c5fbbf8bfb3a2a273d7f73f817),
committed 2026-06-04.

## Entry points

- [`pcb/idp.kicad_pro`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/pcb/idp.kicad_pro)
  is the main KiCad project.
- [`pcb/idp.kicad_pcb`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/pcb/idp.kicad_pcb)
  is the reconstructed motherboard layout and routed netlist.
- [`ai/hardware/schema.md`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/ai/hardware/schema.md)
  is a generated component-pin connectivity reference.
- [`ai/hardware/components.md`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/ai/hardware/components.md)
  and [`ai/hardware/chips`](https://github.com/iskra-delta/Partner40/tree/75b28222770551c5fbbf8bfb3a2a273d7f73f817/ai/hardware/chips)
  identify components and their pinouts.
- [`valid/idp.net`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/valid/idp.net)
  and [`valid/nets.txt`](https://github.com/iskra-delta/Partner40/blob/75b28222770551c5fbbf8bfb3a2a273d7f73f817/valid/nets.txt)
  are useful machine-readable cross-checks.

At the pinned revision, the main `pcb` directory does not contain an
`idp.kicad_sch`. The motherboard logic reference is reconstructed from the
KiCad PCB/netlist plus the generated connectivity documents and original
scans. The `pdm/PartnerPDM.kicad_sch` file is for the separate PDM board, not
the Partner motherboard.

## Emulator relevance

This is the primary board-level reference for the Z80 CPU, DMA, CTC, two SIOs,
floppy controller, RTC/NVRAM, memory banking, clocks, bus buffers, interrupt
daisy chain, and expansion connectors. Use the actual net names and component
pins when checking assumptions currently summarized in
[`PARTNER-HARDWARE-SCHEMATICS.md`](../../books/PARTNER-HARDWARE-SCHEMATICS.md).

## Connections verified against the emulator

- Interrupt priority is CTC, DMA, SIO #1, SIO #2, motherboard PIO, discrete
  FDC request/in-service glue, then the expansion IEI/IEO connection.
- The FDC glue latches the request, supplies the programmable vector during
  acknowledge, blocks lower-priority requests while in service, and releases
  on RETI.
- E51 selects `0000h..07ffh` and E50 selects `0800h..0fffh`. The two 2 KiB
  sockets are not mirrors, and neither is selected at `1000h..1fffh`.
- Incomplete I/O decode creates the documented aliases across `c0h..f7h` and
  the motor-latch aliases at `98h..9fh`.
- MC14411 F13/`XX1` feeds CTC channel 0 at 1600 Hz; ZCTO0 still clocks CTC
  channel 1 for the motor timeout.
- MM58167 periodic/alarm interrupt passes through the inverter and JJ12 option
  to CPU NMI.

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
- The CTC cascade is a literal same-net connection: E67 pin 7 (`ZC/TO0`) and
  pin 22 (`CLK/TRG1`) are both on `_NET_167`. E67 pin 8 (`ZC/TO1`) is `XX2`.
  `XX2` enters E91 pin 12 (74LS32) alongside `RESET+`; E91 pin 11 then passes
  through E94 pins 9/8 (74LS04) to E107 pin 13 (`/CLR2` of the 74LS74 motor
  latch). Thus either reset or a one-clock channel-1 timeout pulse clears
  `MON`. Channel 2's trigger, E67 pin 21, is unconnected. Channel 3's trigger,
  pin 20, is available through JJ10 pin 1.
- DMA E33 pin 25 (`RDY`) is the active-high output of E34 pins 1/2/3
  (74LS32). One input is external expansion `DMARQ-` from J2 pin 18 after E37
  inversion; the other is E107 pin 5, the latched FDC `DRQ+`. The FDC request
  asynchronously sets that flip-flop through E107 pin 4 and a `WRI-` clock
  with D tied low clears it after the transfer write phase. E34 pins 12/13/11
  combine the complementary request (`SIG-`) with `BUSAKB-` to generate the
  FDC's active-low `DACK-`.
- DMA E33 pins 15/14 are `BUSREQ`/`BUSACK`; the board buffers switch the
  address and control buses when the DMA owns them. E33 pin 16 is `CE/WAIT`,
  not `RDY`. Partner ROM programs WR5 as `8ah`, selecting active-high ready
  and CE-only operation, so this board does not request DMA wait extensions.
- FDC E56 pin 16 (`TC`) is not floating. It is driven by E108 pin 11, whose
  74LS08 inputs are `BUSAKB+` and E37 pin 12. E37 inverts the DMA E33 pin 37
  `INT1-`/pulse output. Therefore terminal count reaches the 8272 only when
  the DMA owns the bus and emits its end-of-block pulse. The emulator now
  carries that last-byte event into the FDC instead of ending a transfer merely
  when its in-memory sector buffer is exhausted.
- MM58167 periodic/alarm interrupt passes through the inverter and optional
  JJ12 link to CPU NMI. The standard emulated configuration leaves JJ12 open.

## Regression anchors

- `z80ctc_unit` covers /16 and /256 timing, trigger synchronisation, both edge
  slopes, the zero-as-256 constant, deferred constant replacement, one-clock
  ZC/TO pulses, vector priority, acknowledge, and RETI.
- `partner_board_unit` programs channels 0 and 1 as one-edge counters and
  proves that the first physical `XX1` edge propagates through `_NET_167` and
  clears the motor latch.
- `z80dma_unit` decodes the Zilog manual's sample program and each Partner ROM
  DMA table, then checks ready polarity, completed-byte behaviour after ready
  drops, and byte/burst/continuous bus ownership.
- `disk_controllers_unit` and the ROM boot integrations cover the board's FDC
  and external SASI ready sources through real data transfers. They also cover
  the E108 terminal-count consequence, DMA/non-DMA MSR bit 5, and the different
  ST1 expectations of the original Partner P and Partner G ROMs.

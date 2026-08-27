# Pattern: DMA bus arbitration and ready wiring

Category: Z80 DMA / FDC / SASI / motherboard glue  
Date: 2026-08-26

## Motherboard trace

The motherboard uses E33, a Z8410-compatible DMA. E33 pin 25 is `RDY` and is
driven active high by E34 gate 1 (74LS32, pins 1/2/3):

```text
J2/18 DMARQ- -> E37 inverter --+
                                  +-> E34 OR -> E33/25 RDY
FDC DRQ+ -> E82 inverter ->       |
             E107 request latch --+
```

FDC `DRQ+` asynchronously sets E107's first 74LS74 through `/PRE`. Its Q output
feeds the ready OR gate. `WRI-` clocks the grounded D input, clearing the latch
after the DMA write portion. E107 `/Q` is `SIG-`; E34 combines it with
`BUSAKB-` to produce the FDC's `DACK-`. The other ready source is expansion
connector J2 pin 18, used by the SASI adapter's active-low `DMARQ-` output.

Terminal count is a separate schematic path. FDC E56 pin 16 is E108 pin 11;
the 74LS08 gate combines `BUSAKB+` with E37's inversion of DMA `INT1-`. The
Z80 DMA end-of-block interrupt/pulse therefore becomes active-high 8272 `TC`
only during DMA bus ownership. The board wrapper latches this condition for the
final FDC byte and lets the controller enter result phase from TC.

E33 pins 15 and 14 connect to CPU `BUSREQ` and `BUSACK`. Address and bus-control
buffers select the DMA side while `BUSACK` is asserted, so the DMA is a real
bus master. The interrupt priority is CTC, DMA, SIO 1, SIO 2, motherboard PIO,
FDC glue, then expansion.

E33 pin 16 is the multiplexed `CE/WAIT` input. Partner firmware writes WR5
`8ah`: ready active high, CE-only, no auto restart. Consequently the emulator
does not synthesize a wait source for this board.

## Partner ROM register stream

The ROM byte tables are normal Z8410 programs, not a Partner-only command
language. The common short floppy form is:

```text
79 aa aa nn nn  WR0: A address and N-1 count follow; B temporarily source
14              WR1: A is incrementing memory, standard timing
28              WR2: B is fixed I/O, standard timing
85 pp           WR4: byte mode; B low address pp follows
8a              WR5: ready active high, CE-only
cf              WR6: load addresses and counter
01 or 05        WR0: select B->A or A->B
cf              WR6: load after direction selection
87              WR6: enable DMA
```

The longer `95h` WR4 form also points to an interrupt-control byte. That byte
can in turn point to a pulse-control byte or interrupt vector; the extra ROM
bytes previously mistaken for filler are real associated registers.

The programmed block value is N-1. For example, `ffh,00h` transfers 256 bytes.
`0000h` transfers one byte and `ffffh` represents 65,536 bytes.

## Transfer-mode rules

- Byte mode releases `BUSREQ` after every completed byte and gives the CPU at
  least one full machine cycle before requesting it again. This is the mode
  used by Partner P and Partner G ROMs.
- Burst mode retains the bus while ready remains active. If ready falls during
  a byte, that read/write pair completes and the bus is then released.
- Continuous mode retains the bus even while ready is inactive and resumes the
  next byte when ready returns.
- WR5 selects ready polarity; WR6 `B3h` forces ready internally. All control
  bytes except WR6 `87h` disable DMA requests, so enable must be last.

These rules follow the [Zilog Z80 CPU Peripherals User Manual](https://www.zilog.com/docs/z80/um0081.pdf),
including its Table 16 sample stream
`79 50 10 00 10 14 28 c5 05 8a cf 05 cf 87`. That published example is a
`1001h`-byte burst from memory `1050h` to fixed I/O port `05h` and is reproduced
byte-for-byte in `z80dma_unit`.

## Regression coverage

`z80dma_unit` checks the official sample, every Partner P/G ROM form, WR0-WR6
associated-byte parsing, address types and direction, N-1 count conversion,
active-high/active-low/forced ready, orderly completion after ready falls,
byte-mode CPU gaps, burst release, continuous retention, end-of-block status,
interrupt acknowledge, and RETI. `disk_controllers_unit` and the Partner ROM
boot tests cover both physical ready sources through the FDC and SASI paths.

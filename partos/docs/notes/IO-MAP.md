# Note: Partner I/O Map and Interrupt Topology

Category: Hardware / I/O / Interrupts
Date(s): 2026-06-11

Known I/O map, from the idp-emu emulator and original ROM analysis.
Reference for BIOS driver work.

## I/O Map

| Ports       | Device                                            |
|-------------|---------------------------------------------------|
| 0x10 - 0x12 | SASI adapter (status/ctrl, data, reset)           |
| 0x20 - 0x2F | EF9367 GDP (graphics, GDP model only)             |
| 0x30 - 0x33 | GDP board PIO (video control, GDP model only)     |
| 0x34 - 0x3F | SCN2674 AVDC (text video, GDP model only)         |
| 0x80 - 0x97 | Banking / ROM overlay control (touch-sensitive)   |
| 0x98        | FDC motor: OUT = on, IN bit0 = running status     |
| 0xA0 - 0xBF | MM58167 RTC (NVRAM at 0xA8-0xAF)                  |
| 0xC0        | Z80 DMA                                           |
| 0xC8 - 0xCB | Z80 CTC (ch0->ch1 cascade = FDC motor timeout)    |
| 0xD0 - 0xD3 | Z80 PIO (printer/parallel)                        |
| 0xD8 - 0xDB | Z80 SIO 1 (ch A = keyboard/console)               |
| 0xE0 - 0xE4 | Z80 SIO 2                                         |
| 0xE8        | FDC interrupt vector latch (external IM2 source)  |
| 0xF0 / 0xF1 | i8272 FDC main status / data                      |

## Interrupt Topology

- Zilog daisy chain priority order: **DMA > CTC > SIO1 > SIO2 > PIO**.
- The FDC is *not* on the chain — it raises INT with the vector byte
  programmed at port 0xE8 (board latch), acting as an external IM2 source.

## SASI Adapter Details (ports 0x10-0x12)

- Status read (0x10): bit7=REQ, bit6=IO, bit4=CD, bit3=BSY.
- Control write (0x10): bit0=SEL, bit1=data enable, bit5=DRQ enable.
- Data (0x11), reset (0x12).
- With no controller present the bus floats: reads return 0xFF.

## Floppy Path Details

- i8272 at 0xF0 (MSR) / 0xF1 (data); standard command/execute/result
  phases.
- Motor: OUT 0x98 turns it on; IN 0x98 bit0 reports running; auto-off via
  CTC ch0->ch1 cascade timeout.
- Boot-time sector transfers use the Z80 DMA (port 0xC0) moving bytes from
  FDC data port 0xF1 to memory; DMA RDY is gated on the FDC execute phase.

# Iskra Delta Partner: Complete Reference

_Tomaz Stih, London 2025_  
_Updated for current `idp-emu` behavior, March 2026_

## Introduction

The **Iskra Delta Partner** is a Z80-based professional microcomputer family
developed in Yugoslavia in the 1980s. The machine combines classic Z80 design
patterns (CTC/SIO/PIO daisy-chain peripherals) with banked memory and several
display options.

This document is a practical, emulator-oriented reference. It is written for:

- emulator developers
- firmware/ROM reverse engineers
- system programmers writing Partner-targeted code

It focuses on what is implemented and observable in the current emulator code.
For historical nuances and unresolved hardware questions, see the `docs/notes`
folder.

## Table of Contents

- [Computer Specifications](#computer-specifications)
  - [Models](#models)
  - [Current Bundled Media Names](#current-bundled-media-names)
  - [CPU and Chipset Overview](#cpu-and-chipset-overview)
  - [Display Subsystems](#display-subsystems)
- [I/O Map (Emulator-Verified)](#io-map-emulator-verified)
  - [Base System Ports](#base-system-ports)
  - [GDP Model Additions](#gdp-model-additions)
  - [Notes on SIO and PIO Routing](#notes-on-sio-and-pio-routing)
- [Memory Map (Emulator)](#memory-map-emulator)
  - [Physical Organization](#physical-organization)
  - [Reset and ROM Overlay Behavior](#reset-and-rom-overlay-behavior)
  - [Typical CP/M Logical Layout](#typical-cpm-logical-layout)
- [Emulator-Safe Programming Samples](#emulator-safe-programming-samples)
  - [Sample 1: Disable ROM and Switch Banks](#sample-1-disable-rom-and-switch-banks)
  - [Sample 2: SIO Polling TX/RX on SIO2 Channel A](#sample-2-sio-polling-txrx-on-sio2-channel-a)
  - [Sample 3: RTC Write and Read (MM58167A)](#sample-3-rtc-write-and-read-mm58167a)
  - [Sample 4: PIO Output to Virtual Devices](#sample-4-pio-output-to-virtual-devices)
  - [Sample 5: FDC Motor and Status Poll](#sample-5-fdc-motor-and-status-poll)
- [Virtual Devices (Current Emulator)](#virtual-devices-current-emulator)
  - [SIO Virtual Devices](#sio-virtual-devices)
  - [PIO Virtual Devices](#pio-virtual-devices)
  - [TCP Bridge Control Channel](#tcp-bridge-control-channel)
- [About This Document](#about-this-document)

---

## Computer Specifications

### Models

Common model suffixes:

- `W`: hard disk present
- `1F`: one floppy drive
- `2F`: two floppy drives
- `G`: GDP graphics subsystem

Examples:

- `WF`: hard disk + floppy
- `1FG`: one floppy + GDP
- `WFG`: hard disk + floppy + GDP

### Current Bundled Media Names

Current repository media files:

- ROMs:
  - `roms/partner_crt.rom` (Partner P/CRT path)
  - `roms/partner_gdp.rom` (Partner G/GDP path)
- Floppies:
  - `disks/fdd-partner-p.img` (P model floppy image)
  - `disks/fdd-partner-g.img` (G model floppy image)
- Hard disks:
  - `disks/hdd-partner-g.img` (G model HDD image with startup programs)
  - `disks/hdd-partner-g-empty.img` (empty G model HDD image)

### CPU and Chipset Overview

- CPU: **Z80A**
- Main Zilog peripherals:
  - **Z80 DMA** (`0xC0`)
  - **Z80 CTC** (`0xC8..0xCB`)
  - **Z80 SIO #1** (`0xD8..0xDB`)
  - **Z80 SIO #2** (`0xE0..0xE3`, `0xE4` currently also decoded by helper)
  - **Z80 PIO** (`0xD0..0xD3`)
- Storage:
  - Intel **8272** FDC (`0xF0`, `0xF1`)
  - Xebec **S1410** SASI path (`0x10..0x12`)
- RTC:
  - National **MM58167A** (`0xA0..0xB6`, `0xBF`)

Interrupt daisy-chain priority in emulator tick order:

1. DMA
2. CTC
3. SIO #1
4. SIO #2
5. PIO

### Display Subsystems

- CRT model:
  - text path is serial and tied to SIO1 channel A
  - terminal rendering is handled by the emulator terminal backend
- GDP model:
  - SCN2674 AVDC text subsystem (`0x34..0x3F`)
  - EF9367 graphics subsystem (`0x20..0x2F`)
  - GDP-local PIO (`0x30..0x33`) gates display-side behavior
  - board-level scroll/sync latch behavior on `0x36`

---

## I/O Map (Emulator-Verified)

### Base System Ports

| Port | Dec | Device | Dir | Description |
| ---- | --- | ------ | --- | ----------- |
| `0x10` | 16 | SASI/S1410 | I/O | Status read / control write |
| `0x11` | 17 | SASI/S1410 | I/O | Data read/write |
| `0x12` | 18 | SASI/S1410 | I/O | Error/read side or reset/write side |
| `0x80..0x87` | 128..135 | Banking | I/O | Disable ROM overlay |
| `0x88..0x8F` | 136..143 | Banking | I/O | Select RAM Bank 1 |
| `0x90..0x97` | 144..151 | Banking | I/O | Select RAM Bank 2 |
| `0x98` | 152 | FDC motor | I/O | Motor control/write, motor status/read (`bit0`) |
| `0xA0..0xB6` | 160..182 | MM58167A | I/O | RTC register window |
| `0xBF` | 191 | MM58167A | I/O | RTC test/extra register path |
| `0xC0` | 192 | Z80 DMA | I/O | DMA register port |
| `0xC8..0xCB` | 200..203 | Z80 CTC | I/O | CTC channels |
| `0xD0..0xD3` | 208..211 | Z80 PIO | I/O | Port A/B data/control |
| `0xD8..0xDB` | 216..219 | Z80 SIO #1 | I/O | Channel A/B data/control |
| `0xE0..0xE3` | 224..227 | Z80 SIO #2 | I/O | Channel A/B data/control |
| `0xE8` | 232 | FDC vector | Out | Interrupt vector register |
| `0xF0` | 240 | Intel 8272 | In | Main status register |
| `0xF1` | 241 | Intel 8272 | I/O | Data FIFO/command/result |

### GDP Model Additions

GDP mode extends I/O with EF9367, AVDC, and a local PIO window.

| Port | Dec | Device | Dir | Description |
| ---- | --- | ------ | --- | ----------- |
| `0x20..0x2F` | 32..47 | EF9367 | I/O | GDP command/data/status window |
| `0x30..0x33` | 48..51 | GDP local PIO | I/O | GDP board control PIO |
| `0x34..0x3F` | 52..63 | SCN2674 AVDC | I/O | AVDC text controller window |

Important emulator behavior in GDP mode:

- Port `0x36` is used as a board-level scroll/sync latch path:
  - read: sync bit source
  - write: scroll latch
- AVDC ports `0x36`/`0x37` are intentionally treated as inert in the current
  model.

### Notes on SIO and PIO Routing

- SIO logical channels:
  - `sio1_a`, `sio1_b`, `sio2_a`, `sio2_b`
- `sio1_a` is locked internal:
  - CRT model: `"Internal CRT terminal (fixed)"`
  - GDP model: `"Internal GDP keyboard (fixed)"`
- Attachable serial virtual devices are on free channels:
  - `sio1_b`, `sio2_a`, `sio2_b`

---

## Memory Map (Emulator)

### Physical Organization

- ROM image size: `0x0800` (2 KB)
- Shared RAM base: `0xC000`
- Banked region: `0x0000..0xBFFF` (48 KB)
- Shared region: `0xC000..0xFFFF` (16 KB)
- Two banked RAM images exist in emulator for `0x0000..0xBFFF`.

### Reset and ROM Overlay Behavior

On reset:

- ROM overlay is enabled.
- Reads from `0x0000..0x1FFF` return ROM bytes mirrored from the 2 KB ROM.
- Writes into `0x0000..0x1FFF` are ignored while ROM overlay is enabled.

After writing any value to `0x80..0x87`:

- ROM overlay is disabled.
- RAM becomes visible/writable at low addresses.

### Typical CP/M Logical Layout

The exact layout depends on ROM/loader image and model, but a common CP/M 3
shape on Partner-class systems is:

- `0x0000..0x00FF`: low vectors/system scratch
- `0x0100..`: TPA (user program area)
- upper RAM: CCP/BDOS/BIOS residency blocks

In practice, treat this as firmware-dependent and verify against the boot image
you are running.

---

## Emulator-Safe Programming Samples

The samples below are written to match the current emulator behavior and use
valid Z80 syntax (I/O via register `A`).

### Sample 1: Disable ROM and Switch Banks

```asm
; Disable ROM overlay and toggle RAM banks.

BANK_ROM_OFF    equ     #0x80
BANK_RAM1       equ     #0x88
BANK_RAM2       equ     #0x90

switch_banks:
        xor     a
        out     (BANK_ROM_OFF), a        ; ROM off, RAM visible at 0000h

        xor     a
        out     (BANK_RAM1), a           ; select bank 1

        xor     a
        out     (BANK_RAM2), a           ; select bank 2

        xor     a
        out     (BANK_RAM1), a           ; back to bank 1
        ret
```

### Sample 2: SIO Polling TX/RX on SIO2 Channel A

This avoids the fixed internal `SIO1A` channel.

```asm
SIO2A_DATA      equ     #0xE0
SIO2A_CTRL      equ     #0xE1

; Init SIO2 Channel A for async, 8-bit, RX/TX enabled.
; Works with emulator polling model.
init_sio2a:
        ld      a, #0x04
        out     (SIO2A_CTRL), a          ; select WR4
        ld      a, #0x44                 ; x16 clock, 1 stop, no parity
        out     (SIO2A_CTRL), a

        ld      a, #0x03
        out     (SIO2A_CTRL), a          ; select WR3
        ld      a, #0xC1                 ; RX enable, 8-bit
        out     (SIO2A_CTRL), a

        ld      a, #0x05
        out     (SIO2A_CTRL), a          ; select WR5
        ld      a, #0xEA                 ; DTR+RTS, TX enable, 8-bit
        out     (SIO2A_CTRL), a
        ret

; TX: character in A
sio2a_putc:
        ld      b, a
sio2a_putc_wait:
        in      a, (SIO2A_CTRL)
        bit     2, a                     ; RR0 bit2 = TX buffer empty
        jr      z, sio2a_putc_wait
        ld      a, b
        out     (SIO2A_DATA), a
        ret

; RX: returns character in A
sio2a_getc:
sio2a_getc_wait:
        in      a, (SIO2A_CTRL)
        bit     0, a                     ; RR0 bit0 = RX char available
        jr      z, sio2a_getc_wait
        in      a, (SIO2A_DATA)
        ret
```

### Sample 3: RTC Write and Read (MM58167A)

```asm
RTC_SEC         equ     #0xA1
RTC_MIN         equ     #0xA2
RTC_HOUR        equ     #0xA3
RTC_DAY         equ     #0xA5
RTC_MONTH       equ     #0xA7
RTC_RESETCNT    equ     #0xB2

rtc_set_example:
        ld      a, #0x45                 ; 45 sec (BCD)
        out     (RTC_SEC), a
        ld      a, #0x30                 ; 30 min (BCD)
        out     (RTC_MIN), a
        ld      a, #0x14                 ; 14 hour (BCD)
        out     (RTC_HOUR), a
        ld      a, #0x25                 ; day 25 (BCD)
        out     (RTC_DAY), a
        ld      a, #0x04                 ; month 04 (BCD)
        out     (RTC_MONTH), a

        xor     a
        out     (RTC_RESETCNT), a        ; refresh/sync path
        ret

rtc_read_example:
        in      a, (RTC_SEC)
        ld      (rtc_sec_bcd), a
        in      a, (RTC_MIN)
        ld      (rtc_min_bcd), a
        in      a, (RTC_HOUR)
        ld      (rtc_hour_bcd), a
        in      a, (RTC_DAY)
        ld      (rtc_day_bcd), a
        in      a, (RTC_MONTH)
        ld      (rtc_month_bcd), a
        ret

rtc_sec_bcd:    db      #0x00
rtc_min_bcd:    db      #0x00
rtc_hour_bcd:   db      #0x00
rtc_day_bcd:    db      #0x00
rtc_month_bcd:  db      #0x00
```

### Sample 4: PIO Output to Virtual Devices

If a virtual Covox or visual Centronics printer is attached in the **Devices**
panel, data writes to PIO data ports are consumed by the attached device.

```asm
PIOA_DATA       equ     #0xD0
PIOA_CTRL       equ     #0xD1
PIOB_DATA       equ     #0xD2
PIOB_CTRL       equ     #0xD3

; Put Port A in Mode 0 (output), then write one byte.
pioa_write_byte:
        ld      a, #0x0F                 ; mode set, mode 0
        out     (PIOA_CTRL), a
        ld      a, #'H'
        out     (PIOA_DATA), a           ; goes to attached PIO-A virtual device
        ret

; Put Port B in Mode 0 (output), then write one byte.
piob_write_byte:
        ld      a, #0x0F
        out     (PIOB_CTRL), a
        ld      a, #'I'
        out     (PIOB_DATA), a
        ret
```

### Sample 5: FDC Motor and Status Poll

```asm
FDC_MOTOR       equ     #0x98
FDC_STATUS      equ     #0xF0

fdc_motor_on_and_check:
        ld      a, #0x01
        out     (FDC_MOTOR), a           ; emulator latches motor on

        in      a, (FDC_MOTOR)
        and     #0x01                    ; bit0 = motor running
        ret                               ; Z=0 means motor is on

; Poll Intel 8272 MSR until RQM=1 (bit7)
fdc_wait_rqm:
        ld      b, #0x00                 ; timeout (256 loops)
fdc_wait_rqm_loop:
        in      a, (FDC_STATUS)
        bit     7, a
        ret     nz
        djnz    fdc_wait_rqm_loop
        scf                               ; timeout indicator
        ret
```

---

## Virtual Devices (Current Emulator)

### SIO Virtual Devices

Per free SIO port, device options are:

- `None`
- `Serial Mouse (Microsoft)`
- `Serial Mouse (Mouse Systems)`
- `Serial Mouse (Logitech)`
- `TCP Bridge`

Logitech-specific behavior includes prompt/poll handling:

- `c` -> identification string (`LOGIMOUSE C7 ...`)
- `P` -> 5-byte C7 poll report
- `D` -> prompt-mode no-op acceptance

Logitech capture behavior in SDL UI:

- auto-capture on window enter
- release on leave
- toggle on `F12`

### PIO Virtual Devices

Per PIO port:

- `None`
- `Covox DAC`
- `Centronics Printer (Visual)`

Data writes to `0xD0`/`0xD2` are consumed by attached device runtime.

### TCP Bridge Control Channel

Control socket supports:

- `PING`
- `CTS 0|1|AUTO`
- `DCD 0|1|AUTO`

And reports modem output state changes:

- `RTS <0|1>`
- `DTR <0|1>`

---

## About This Document

This reference is maintained alongside the emulator and should be updated when:

- new port behavior is implemented
- register semantics are changed
- virtual device routing is extended

Primary implementation sources:

- `src/partner.cpp`
- `src/partner_crt.cpp`
- `src/partner_gdp.cpp`
- `lib/chipsex/zilog/z80sio.h`
- `lib/chipsex/zilog/z80pio.h`

Related notes:

- `docs/notes/patterns/SIO-PIO-TCP-VIRTUAL-DEVICES.md`
- `docs/notes/patterns/SIO-KEYBOARD-PATH.md`
- `docs/notes/patterns/INTERRUPT-IM2-DAISYCHAIN.md`

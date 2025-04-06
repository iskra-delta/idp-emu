# Iskra Delta Partner: The Complete Reference

_Tomaz Stih, London 2025_

# Introduction

The **Iskra Delta Partner** was a powerful 8-bit microcomputer developed in 1980s Yugoslavia by Iskra Delta, designed for military, business, technical, and professional use. Based on the Zilog Z80A processor, the Partner offered a modular, banked, and expandable architecture—unusual and advanced for its time.

At its core, the Partner features up to 64 KB of addressable RAM, and multiple configurations supporting floppy disks, hard disks, real-time clock functionality, and a choice of text-only or graphical display systems.

Depending on the model, the Partner came equipped with different display subsystems:

- **Text-only models** used a **custom CRT controller board**, with screen output handled via a serial connection through the Z80 SIO. Input and output were character-based, and managed entirely over this serial channel.

- **GDP models** included both a **Signetics SCN2674 AVDC** for hardware text display and a **Thomson EF9367 Graphics Display Processor (GDP)** for high-resolution raster graphics. These two processors shared the screen space, allowing mixed text and graphics output—rare among Z80-based systems.

This document provides a complete and detailed technical reference for the Partner, including:

- **Z80 CPU core** and memory banking
- **Memory map**, including RAM, ROM, and CP/M layout
- **Display subsystems**:
  - **Text-only CRT** via SIO (non-GDP models)
  - **AVDC + GDP** (GDP models only)
- **Floppy disk controller** based on Intel 8272
- **Hard disk controller** using the Xebec S1410 SASI interface
- **Real-time clock** (MM58167A) with alarm and NVRAM
- **Peripheral I/O**, including:
  - Z80 SIO for serial communication
  - Z80 PIO for parallel ports
  - Z80 CTC for counters/timers (when present)
- **ROM disassembly insights**, covering bootloader, memory tests, and device initialization

This reference draws from ROM disassembly, original documentation, and direct hardware research. It is meant for programmers, hardware tinkerers, emulator developers, and digital preservationists working with the Partner system.

Whether you're reverse-engineering the ROM, restoring an original machine, or writing an emulator, this guide offers comprehensive and authoritative information on every aspect of the Iskra Delta Partner.

## Table of Contents

- [Computer Specifications](#computer-specifications)
  - [Models](#models)
  - [I/O Map](#io-map)
    - [Xebec S1410 SASI Hard Disk Controller](#xebec-s1410-sasi-hard-disk-controller)
    - [Shared Graphics and Text Control](#shared-graphics-and-text-control)
    - [Text Output (Signetics SCN2674)](#text-output-signetics-scn2674)
    - [Memory Banking and EPROM Control](#memory-banking-and-eprom-control)
    - [MM58167A Real-Time Clock](#mm58167a-real-time-clock)
    - [Z80 PIO: Parallel Ports](#z80-pio-parallel-ports)
    - [Z80 SIO: Serial Ports](#z80-sio-serial-ports)
    - [8272 Floppy Disk Controller](#8272-floppy-disk-controller)
  - [Memory Map](#memory-map)
    - [ROM](#rom)
    - [RAM](#ram)
    - [CP/M Memory Layout](#cpm-memory-layout)
- [About This Document](#about-this-document)

---

## Computer Specifications

### Models

The following Iskra Delta Partner models were produced: `WF`, `1F`, `2F`, `WFG`, `1FG`, and `2FG`.

Model code breakdown:

- `W` – Includes a hard disk
- `1F` – One floppy disk drive
- `2F` – Two floppy disk drives
- `G` – Includes a Thomson EF9367 graphics card

For example, a `WFG` model includes a hard disk, a single floppy disk, and the graphical processor.

---

## I/O Map

### Xebec S1410 SASI Hard Disk Controller

| Port | Dec | Description | Dir | Notes                  |
| ---- | --- | ----------- | --- | ---------------------- |
| 0x10 | 16  | RDSTAT      | In  | Status read            |
| 0x11 | 17  | RDDATA      | In  | Data read              |
| 0x12 | 18  | ERROR       | In  | Error code             |
| 0x10 | 16  | WRCONTR     | Out | Control register write |
| 0x11 | 17  | WRDATA      | Out | Data write             |
| 0x12 | 18  | RESET       | Out | Controller reset       |

Early units used Tandon drives; later ones used Seagate ST-412. Disk parameters are set from the EPROM at startup.

### Shared Graphics and Text Control

These ports are shared between the EF9367 graphics display processor and the SCN2674 AVDC character display.

| Port | Dec | Description            | Dir | Notes                         |
| ---- | --- | ---------------------- | --- | ----------------------------- |
| 0x30 | 48  | Graphics control       | I/O | Shared control signals        |
| 0x31 | 49  | PIO Port A Control     | Out | Possibly unused or repurposed |
| 0x32 | 50  | Common text attributes | I/O | Shared visual attribute bus   |
| 0x33 | 51  | PIO Port B Control     | Out | Possibly unused or repurposed |

Note: Ports 0x31 and 0x33 _appear_ to shadow the Z80 PIO at 0xD1 and 0xD3 but are likely separate lines for AVDC/GDP internal signal control, as suggested in the ROM init routine.

### Text Output (Signetics SCN2674)

Used by the AVDC for character-oriented text display. Supports 80×25 and 132×26 modes, row addressing, split-screen.

| Port | Dec | Description                   | Dir | Notes                     |
| ---- | --- | ----------------------------- | --- | ------------------------- |
| 0x34 | 52  | Character register            | I/O | Writes characters         |
| 0x35 | 53  | Attribute register            | I/O | Color, blink, underline   |
| 0x36 | 54  | Scroll (W) / Common Input (R) | I/O | Scrolls text; GDP bridge  |
| 0x38 | 56  | Init / Interrupt status       | I/O | Init pointer or IRQ flags |
| 0x39 | 57  | Command / Status              | I/O | Control commands          |
| 0x3A | 58  | Screen Start 1 Low            | I/O | Lower byte of screen addr |
| 0x3B | 59  | Screen Start 1 High           | I/O | Upper byte of screen addr |
| 0x3C | 60  | Cursor Address Low            | I/O |                           |
| 0x3D | 61  | Cursor Address High           | I/O |                           |
| 0x3E | 62  | Screen Start 2 Low            | I/O | Split-screen feature      |
| 0x3F | 63  | Screen Start 2 High           | I/O |                           |

The command register (0x39) supports master reset (`0x00`), cursor enable/disable, write/read-at-cursor, and split screen handling.

### Memory Banking and EPROM Control

| Port | Dec | Description                 | Dir | Notes                               |
| ---- | --- | --------------------------- | --- | ----------------------------------- |
| 0x80 | 128 | Disable 4KB EPROM           | I/O | RAM at 0x0000-0x1FFF becomes usable |
| 0x88 | 136 | Select RAM Bank 1 (default) | I/O | On reset                            |
| 0x90 | 144 | Select RAM Bank 2           | I/O | Alternate bank                      |

Other ports in the 0x81–0x8F range are unused or undocumented.

<details>
<summary>Example: Set and Get Date/Time (MM58167A) (in BCD!)
</summary>

```asm
;===============================================================================
; Disable ROM and toggle RAM banks
;===============================================================================

switch_banks:
        ; Disable ROM at 0000h–07FFh (Enable RAM there)
        ld      a, #0x00
        out     (#0x80), a                       ; Disable EPROM (shows RAM)

        ; Select RAM Bank 1 (default)
        ld      a, #0x00
        out     (#0x88), a                       ; Select Bank 1

        ; Select RAM Bank 2
        ld      a, #0x00
        out     (#0x90), a                       ; Select Bank 2

        ; Switch back to RAM Bank 1
        ld      a, #0x00
        out     (#0x88), a                       ; Select Bank 1 again

        ret
```

</details>

### MM58167A Real-Time Clock

BCD-based RTC with alarm, interrupt, and NVRAM.

| Port      | Dec     | Description                       | Dir   | Notes      |
| --------- | ------- | --------------------------------- | ----- | ---------- |
| 0xA0–0xA7 | 160–167 | Time registers (1/1000s to month) | I/O   | BCD format |
| 0xA8–0xAF | 168–175 | Alarm registers (NVRAM)           | I/O   | 9 bytes    |
| 0xB0      | 176     | Interrupt status                  | In    |            |
| 0xB1      | 177     | Interrupt control                 | I/O   |            |
| 0xB2      | 178     | Reset counter                     | Out   |            |
| 0xB3      | 179     | Reset NVRAM flags                 | Out   |            |
| 0xB4–0xB6 | 180–182 | Status and standby                | Mixed |            |
| 0xBF      | 191     | Chip test mode                    | ?     |            |

<details>
<summary>Example: Set and Get Date/Time (MM58167A) (in BCD!)
</summary>

```asm
;===============================================================================
; Write Date and Time to MM58167A
;===============================================================================
; Sets:
;   - Seconds = 45
;   - Minutes = 30
;   - Hours   = 14
;   - Day     = 25
;   - Month   = 4
;===============================================================================

set_datetime:
        ; Seconds (#0x45 = 45 BCD)
        ld      a, #0x45
        out     (#0xA1), a                            ; Seconds

        ; Minutes (#0x30 = 30 BCD)
        ld      a, #0x30
        out     (#0xA2), a                            ; Minutes

        ; Hours (#0x14 = 14 BCD)
        ld      a, #0x14
        out     (#0xA3), a                            ; Hours

        ; Day (#0x25 = 25 BCD)
        ld      a, #0x25
        out     (#0xA5), a                            ; Day

        ; Month (#0x04 = April)
        ld      a, #0x04
        out     (#0xA7), a                            ; Month

        ret

;===============================================================================
; Read Date and Time from MM58167A
;===============================================================================
; Returns values in BCD in registers:
;   E = Seconds
;   D = Minutes
;   C = Hours
;   B = Day
;   A = Month
;===============================================================================

get_datetime:
        in      e, (#0xA1)                            ; Read Seconds
        in      d, (#0xA2)                            ; Read Minutes
        in      c, (#0xA3)                            ; Read Hours
        in      b, (#0xA5)                            ; Read Day
        in      a, (#0xA7)                            ; Read Month

        ret

```

</details>

---

### Z80 PIO: Parallel Ports

| Port | Dec | Description        | Dir | Model |
| ---- | --- | ------------------ | --- | ----- |
| 0xD0 | 208 | PIO Port A Data    | I/O | All   |
| 0xD1 | 209 | PIO Port A Control | I/O | All   |
| 0xD2 | 210 | PIO Port B Data    | I/O | All   |
| 0xD3 | 211 | PIO Port B Control | I/O | All   |

### Z80 SIO: Serial Ports

Dual SIO channels for CRT, printer, and host.

| Port      | Dec     | Description                      | Dir | Use |
| --------- | ------- | -------------------------------- | --- | --- |
| 0xD8–0xDB | 216–219 | SIO Channel 1 (Keyboard/Printer) | I/O |     |
| 0xE0–0xE4 | 224–228 | SIO Channel 2 (Host/VAX)         | I/O |     |

<details>
<summary>Example: Interrupt-Driven Serial I/O Routine Using Z80 SIO (Channel A)
</summary>

```asm
SIO1_CTRL_A    equ   #0xD9                             ; SIO1 Channel A control port
SIO1_DATA_A    equ   #0xD8                             ; SIO1 Channel A data port
INT_VEC_TABLE  equ   #0x0200                           ; IM2 vector table location
ISR_ADDRESS    equ   serial_isr                        ; Actual ISR routine

;---------------------------------------
; Setup interrupt mode and vector table
;---------------------------------------
init_interrupts:
        di                                            ; Disable interrupts
        ld    a, #0x02                                 ; Interrupt page = 0x02
        ld    i, a                                     ; Set high byte of vector
        ld    hl, INT_VEC_TABLE
        ld    de, INT_VEC_TABLE + 1
        ld    bc, #0x0100                              ; Fill 256 bytes
        ld    (hl), low(ISR_ADDRESS)                   ; Fill with ISR address
        ldir
        im    2                                        ; Interrupt Mode 2
        ei                                            ; Enable interrupts
        ret

;---------------------------------------
; SIO Channel A Initialization (9600 8N1)
;---------------------------------------
init_sio1:
        ld    c, SIO1_CTRL_A
        ld    hl, sio1_init_data
        ld    b, #0x07                                 ; 7 control words
        otir                                           ; Send init data to SIO
        ret

sio1_init_data:
        db   #0x18                                     ; WR0: Point to WR1
        db   #0x04                                     ; WR1: Enable RX interrupt only
        db   #0x05                                     ; WR2: Vector base = 0x00
        db   #0x04                                     ; WR3: RX enable, 8-bit
        db   #0x44                                     ; WR4: 1 stop, 8-bit, async
        db   #0x00                                     ; WR5: TX disabled for now
        db   #0x10                                     ; WR0: Reset ext/status interrupts

;---------------------------------------
; Install SIO ISR (simple echo routine)
;---------------------------------------
serial_isr:
        push af
        in    a, (SIO1_CTRL_A)                         ; Read RR0: interrupt reason
        bit   2, a                                     ; RX ready?
        jr    z, .done

        in    a, (SIO1_DATA_A)                         ; Read incoming byte
        out   (SIO1_DATA_A), a                         ; Echo it back

.done:
        pop   af
        ei                                            ; Enable next interrupt
        reti                                           ; Return from interrupt
```

</details>

---

### Z80 DMA

| Port | Dec | Description  | Dir | Notes        |
| ---- | --- | ------------ | --- | ------------ |
| 0xC0 | 192 | DMA Register | ?   | Possibly DMA |

<details>
<summary>Example: DMA Memory Copy: #0x8000 ➝ #0x4000, size #0x4000
</summary>

```asm
        ld      c, #0xC0                              ; DMA port
        ld      hl, dma_program                       ; DMA program in memory
        ld      b, #0x11                              ; 17 bytes to write
        otir                                          ; Send to DMA controller

        ld      a, #0x01                              ; Start DMA transfer
        out     (#0xDF03), a                          ; Trigger transfer
        ret

;-----------------------------------------------
; DMA program: 17 bytes for memory copy
;-----------------------------------------------
dma_program:
        db      #0xC3                                 ; Command: memory-to-memory
        db      #0x05                                 ; Channel (src/dest interleaved)

        ; Source address (start at 0x8000)
        db      #0x00, #0x80                          ; Source address low, high

        ; Destination address (start at 0x4000)
        db      #0x00, #0x40                          ; Dest address low, high

        ; Transfer length (0x4000 bytes)
        db      #0x00, #0x40                          ; Count low, high

        ; Source page
        db      #0x80

        ; Destination page
        db      #0x40

        ; Mode register
        db      #0x45                                 ; Mode: mem→mem, increment

        ; Misc/Timing
        db      #0xF1                                 ; Misc
        db      #0x8A                                 ; Timing
        db      #0xCF                                 ; Master control
        db      #0x01, #0xCF, #0x87                   ; Final bytes (specific to IDP ROM)

```

</details>

---

### 8272 Floppy Disk Controller

| Port | Dec | Description                 | Dir | Notes         |
| ---- | --- | --------------------------- | --- | ------------- |
| 0x98 | 152 | FDC Motor On / Motor status | I/O | Bit 1 = motor |
| 0xC0 | 192 | DMA Register (?)            | ?   | Possibly DMA  |
| 0xE8 | 232 | Interrupt Vector Register   | ?   |               |
| 0xF0 | 240 | FDC Status                  | In  |               |
| 0xF1 | 241 | FDC Data                    | I/O |               |

<details>
<summary>Example: Boot Sector Read Example (with #0x prefix and proper alignment)
</summary>

```asm
FDC_STATUS     equ   #0xF0                             ; FDC status register (read)
FDC_DATA       equ   #0xF1                             ; FDC data register (read/write)
LOAD_ADDR      equ   #0x8000                           ; Boot sector destination
BUFFER         equ   LOAD_ADDR

;---------------------------------------
; Wait until FDC is ready to accept data
;---------------------------------------
wait_fdc_ready:
        in    a, (FDC_STATUS)                          ; Read FDC status
        and   #0xC0                                    ; Mask RQM and DIO
        cp    #0x80                                    ; RQM=1, DIO=0?
        jr    nz, wait_fdc_ready                       ; Loop until ready
        ret

;---------------------------------------
; Issue READ SECTOR command (sector 1)
;---------------------------------------
fdc_read_sector:
        call  wait_fdc_ready
        ld    a, #0x06                                 ; READ SECTOR command
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x00                                 ; Drive 0, Head 0
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x00                                 ; Track 0
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x00                                 ; Head 0
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x01                                 ; Sector 1
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x02                                 ; 512 byte sector size (2^2)
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x01                                 ; One sector
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0x1B                                 ; GAP3 length
        out   (FDC_DATA), a

        call  wait_fdc_ready
        ld    a, #0xFF                                 ; DTL (don’t care)
        out   (FDC_DATA), a

;---------------------------------------
; Wait for result phase and read results
;---------------------------------------
wait_result_phase:
        in    a, (FDC_STATUS)
        and   #0xC0
        cp    #0x80
        jr    nz, wait_result_phase

        ld    b, #0x07                                 ; Expect 7 result bytes
read_result:
        call  wait_fdc_ready
        in    a, (FDC_DATA)                            ; Read result byte
        djnz  read_result
        ret
```

</details>

---

## Memory Map

#### Partner Physical Memory Map Diagram

```
+--------------------------+  FFFFh
|      Shared RAM          |  (Always visible, not banked,
|      (16 KB)             |   Used for data sharing, IRQs, buffers)
+--------------------------+  C000h
|                          |
|                          |
|     Banked RAM           |  (48 KB switchable via OUT #0x88/#0x90
|                          |   Visible at 1000h–BFFFh)
|                          |
+--------------------------+  1000h
|   Empty ROM Socket Area  |  (Physical 4 KB socket partially used)
+--------------------------+  0800h
|       ROM (2 KB)         |  (Enabled on reset
|      (EPROM active)      |   Can be disabled with OUT #0x80)
+--------------------------+  0000h
```

#### Partner Logical Memory Map Diagram

```
+-------------------------+  FFFFh
|     Shared Memory       |  (Shared RAM, interrupt vectors, etc.)
|    (Banked 3 KB)        |
|-------------------------|
|     CP/M BIOS           |  F000h - FFFFh
|-------------------------|
|     CP/M BDOS           |  E400h - EFFFh (typical)
|-------------------------|
|     CP/M CCP            |  DC00h - E3FFh (typical)
|-------------------------|
|  Transient Program Area |  0100h - DBFFh
|   (User Application)    |
|-------------------------|
|    System I/O Buffers   |  0000h - 00FFh
+-------------------------+  0000h

```

---

## About This Document

**Iskra Delta Partner: The Complete Reference** is maintained by Tomaz Stih.

If you find corrections, improvements, or have historical insights into the Iskra Delta Partner platform, your contributions are welcome.

---

# Iskra Delta Partner: The Complete Reference

# Table of Content

- [Computer Specs](#computer-specs)
  * [Models](#models)
  * [I/O Map](#i-o-map)
    * [Hard disk controller Winchester SASI](#hard-disk-controller-winchester-sasi)
    * [Shared text and graphics control](#shared-text-and-graphics-control)
    * [Text output registers Signetics](#text-output-registers-signetics)
    * [Memory banks and EPROM control](#memory-banks-and-eprom-control)
    * [Realtime clock ports MM58167A](#realtime-clock-ports-mm58167a)
    * [Parallel ports Z80 PIO](#parallel-ports-z80-pio)
    * [Serial ports Z80 SIO](#serial-ports-z80-sio)
    * [Floppy disk controller 8272](#floppy-disk-controller-8272)
  * [Memory Map](#memory-map)
    + [ROM](#rom)
    + [RAM](#ram)
    + [CP/M memory layout](#cp-m-memory-layout)
- [About This Document](#about-this-document)

# Computer Specs

## Models

Following combinations were known to be produced: `WF`, `1F`, `2F`, `WFG`, `1FG`, and `2FG`.

Individual letters in model name have the following meaning:
 * W model has hard disk
 * 1F model has one floppy disk
 * 2F model has two floppy disks
 * G model has Thomson graphical card

Hence, a `WFG` model would be a model with a floppy, a hard disk, and graphical capability.

## I/O map

### Hard disk controller Winchester SASI

| Port | Dec | Description       | Notes | Model |
|------|-----|-------------------|-------|-------|
| 0x10 | 16  | RDSTAT register   | R     | All   |
| 0x11 | 17  | RDDATA register.  | R     | All   |
| 0x12 | 18  | ERROR register    | R     | All   |
| 0x10 | 16  | WRCONTR register. | W     | All   |
| 0x11 | 17  | WRDATA register.  | W     | All   |
| 0x12 | 18  | RESET             | W     | All   |

HDD is Seagate ST-412. Early versions shipped with the TANDON HDD. HDD settings can be obtained from the EPROM. 

### Shared text and graphics control

These ports control both: Thompson and Signetics cards.

| Port | Dec | Description              | Notes | Model |
|------|-----|--------------------------|-------|-------|
| 0x30 | 48  | Graphics common control  | R/W   | All   |
| 0x31 | 49  | PIO, PORT A, CONTROL     | W     | All   |
| 0x32 | 50  | Common text attributes   | R/W   | All   |
| 0x33 | 51  | PIO, PORT B, CONTROL     | W     | All   |

It is not well understood what PIO registers are. They could be mapped to
Z80 PIO, but Z80 PIO already has its' control ports 0xD1 and 0xD3.

### Text output registers Signetics

| Port | Dec | Description              | Notes | Model |
|------|-----|--------------------------|-------|-------|
| 0x34 | 52  | Character register       | R/W   | All   |
| 0x35 | 53  | Attribute register       | R/W   | All   |
| 0x36 | 54  | W:scroll, R:common input | R/W   | All   |
| 0x38 | 56  | W: init, R: interrupt    | R/W   | All   |
| 0x39 | 57  | W:command, R: status     | R/W   | All   |
| 0x3A | 58  | Screen start low         | R/W   | All   |
| 0x3B | 59  | Screen start high        | R/W   | All   |
| 0x3C | 60  | Cursor address low       | R/W   | All   |
| 0x3D | 61  | Cursor address high      | R/W   | All   |
| 0x3E | 62  | Screen start 2 low       | R/W   | All   |
| 0x3F | 63  | Screen start 2 high      | R/W   | All   |

### Memory banks and EPROM control

| Port      | Dec     | Description                                                       | Notes          | Model |
|-----------|---------|-------------------------------------------------------------------|----------------|-------|
| 0x80      | 128     | Read or write to this port switches off 4KB EPROM. After this operation the RAM *behind* 0x0000 to 0x2000 becomes available. | - | All   |
| 0x81-0x87 | 129-135 | -                                                                 | -              | -     |
| 0x88      | 136     | Read or write to this port switches on RAM BANK 1. This is the default bank after the reset | - | All   |
| 0x89-0x8F | 137-143 | -                                                                 | -              | -     |
| 0x90      | 144     | Read or write to this port switch on RAM BANK 2.                                             | - | All   |

### Realtime clock ports MM58167A

| Port | Dec | Description                                                      | Notes | Model |
|------|-----|------------------------------------------------------------------|-------|-------|
| 0xA0 | 160 | BCD 1/1000sec (only high nibble is used, low nibble is always 0) | R/W   | All   |
| 0xA1 | 161 | BCD 1/100sec                                                     | R/W   | All   |
| 0xA2 | 162 | BCD second                                                       | R/W   | All   |
| 0xA3 | 163 | BCD minute                                                       | R/W   | All   |
| 0xA4 | 164 | BCD hour                                                         | R/W   | All   |
| 0xA5 | 165 | BCD weekday                                                      | R/W   | All   |
| 0xA6 | 166 | BCD day of month                                                 | R/W   | All   |
| 0xA7 | 167 | BCD month                                                        | R/W   | All   |
| 0xA8 | 168 | NVRAM byte 0 (battery powered RAM, 1/1000s for alerts).          | R/W   | All   |
| 0xA9 | 169 | NVRAM byte 1 (1/100s for alerts)                                 | R/W   | All   |
| 0xAA | 170 | NVRAM byte 3 (sec for alerts)                                    | R/W   | All   |
| 0xAB | 171 | NVRAM byte 4 (min for alerts)                                    | R/W   | All   |
| 0xAC | 172 | NVRAM byte 5 (hour for alerts)                                   | R/W   | All   |
| 0xAD | 173 | NVRAM byte 6 (weekday for alerts)                                | R/W   | All   |
| 0xAE | 174 | NVRAM byte 7 (day of month for alert)                            | R/W   | All   |
| 0xAF | 175 | NVRAM byte 8 (month)                                             | R/W   | All   |
| 0xB0 | 176 | Interrupt status register                                        | R     | All   |
| 0xB1 | 177 | Interrupt control register                                       | R/W   | All   |
| 0xB2 | 178 | Reset counter                                                    | W     | All   |
| 0xB3 | 179 | Reset NVRAM (bit 0=1/1000s ... bit 7=month)                      | W     | All   |
| 0xB4 | 180 | Status bit                                                       | R     |  All  |
| 0xB5 | 181 | GO Command                                                       | W     | All   |
| 0xB6 | 182 | Standby interrupt                                                | ?     | All   |
| 0xBF | 191 | Chip test mode                                                   | ?     | All   |

### Parallel ports Z80 PIO

| Port | Dec | Description          | Notes | Model |
|------|-----|----------------------|-------|-------|
| 0xD0 | 208 | PIO, PORT A, DATA    | R/W   | All   |
| 0xD1 | 209 | PIO, PORT A, CONTROL | R/W   | All   |
| 0xD2 | 210 | PIO, PORT B, DATA    | R/W   | All   |
| 0xD3 | 211 | PIO, PORT B, CONTROL | R/W   | All   |

### Z80 CTC ports C8-CF


### Serial ports Z80 SIO

| Port | Dec | Description            | Notes | Model |
|------|-----|------------------------|-------|-------|
| 0xD8 | 216 | SIO 1, PORT A, DATA    | R/W   | All   |
| 0xD9 | 217 | SIO 1, PORT A, CONTROL | R/W   | All   |
| 0xDA | 218 | SIO 1, PORT B, DATA    | R/W   | All   |
| 0xDB | 219 | SIO 1, PORT B, CONTROL | R/W   | All   |
| 0xE0 | 224 | SIO 2, PORT A, DATA    | R/W   | All   |
| 0xE1 | 225 | SIO 2, PORT A, CONTROL | R/W   | All   |
| 0xE3 | 227 | SIO 2, PORT B, DATA    | R/W   | All   |
| 0xE4 | 228 | SIO 2, PORT B, CONTROL | R/W   | All   |

### Floppy disk controller 8272

| Port | Dec | Description                                                       | Notes                                                          | Model |
|------|-----|-------------------------------------------------------------------|----------------------------------------------------------------|-------|
| 0x98 | 152 | FDC Motor on. The motor stops with signals RESET and XX2 counter. | Reading this port returns motor status in bit 1 (1=on, 0=off). | All   |
| 0xC0 | 192 | DMA Reg.                                                          | -                                                              | All   |
| 0xE8 | 232 | RDC Interrupt Vector.                                             | -                                                              | All   |
| 0xF0 | 240 | FDC Status.                                                       | -                                                              | All   |
| 0xF1 | 241 | FDC Data.                                                         | -                                                              | All   |
| 0xE8 | 232 | RDC Interrupt Vector                                              | -                                                              | All   |

Floppy disk is TM 10C-4.

### Memory mapped ports

Partner has no memory mapped ports.

## Memory map

### ROM

### RAM

### CP/M memory layout

## Video

### Text

### High resolution graphics

## Keyboard

## Hard disk

## Floppy disk

## Real Time Clock

## Serial communication

## Parallel ports

## Direct memory access

## Counters and timers

# The ROM dissasembly

; Read data from the hard disk drive using the Xebec S1410 controller
067F   IN A,(#10)         ; Read the status information from the controller by inputting a value from port 0x10 into the A register
0681   AND #08            ; Check the 4th bit of the status byte by performing a bitwise AND operation with the value 0x08. This bit indicates whether the controller is currently busy performing an operation.
0683   JP Z,#068B         ; Jump to address 0x068B if the 4th bit of the status byte is 0 (i.e., if the controller is not busy)
0686   LD A,#41           ; Load the A register with the value 0x41, which is likely a command or operation code that is sent to the controller.
0688   JP #06D7           ; Call subroutine at address 0x06D7 to send the command to the hard disk drive or controller. This subroutine likely writes the command to port 0x11 and sets the appropriate bits in the command byte at port 0x10.



xs1410_not_busy:



xebec_not_busy:
068B   LD A,#01           ; Load the A register with the value 0x01.
068D   OUT (#10),A        ; Write the value in the A register to port 0x10. This sets the 0th bit of the command byte (the bit that indicates a read operation) and starts the operation.
xebec_loop:
068F   IN A,(#10)         ; Read the status information from the controller by inputting a value from port 0x10 into the A register.
0691   AND #08            ; Check the 4th bit of the status byte by performing a bitwise AND operation with the value 0x08. This bit indicates whether the controller is currently busy performing an operation.
0693   JP Z,#068F         ; Jump to address 0x068F if the 4th bit of the status byte is 0 (i.e., if the controller is not busy). This creates a loop that waits for the operation to complete.
0696   LD A,#02           ; Load the A register with the value 0x02, which is likely a command or operation code that is sent to the controller.
0698   OUT (#10),A        ; Write the value in the A register to port 0x10. This sets the 1st bit of the command byte (the bit that indicates a reset operation) and resets the controller.
069A   RET                ; Return from the subroutine.


# References

<a id="1">[1]</a>
M. Korth (2001-2012),
Sinclair ZX Specifications, 
[Online.](https://problemkaputt.de/zxdocs.htm)

# About This Document

**Iskra Delta Partner: The Complete Reference** is maintained by Tomaz Stih.

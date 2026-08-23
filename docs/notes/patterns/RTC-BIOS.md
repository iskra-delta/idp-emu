# Pattern: Real-Time Clock and BIOS NVRAM Usage

Category: RTC / NVRAM / BIOS  
Date(s): 2021-05-22 to 2021-05-25

## Problem / Purpose

Document the hardware behavior and software usage of the MM58167A real-time
clock chip in Iskra Delta Partner, including NVRAM register usage and how the
SET UP program stores BIOS configuration data.

## Findings / Observations

### Hardware Overview

The Partner uses the National Semiconductor MM58167A RTC chip. It exposes 32 registers through ports `0xA0` to `0xBF`.

### RTC Time Registers

| Port | Description                                  |
| ---- | -------------------------------------------- |
| 0xA0 | 1/1000 sec counter (or 1/10000 in some docs) |
| 0xA1 | 1/100 sec                                    |
| 0xA2 | Seconds (BCD)                                |
| 0xA3 | Minutes (BCD)                                |
| 0xA4 | Hours (BCD)                                  |
| 0xA5 | Day of week                                  |
| 0xA6 | Day of month                                 |
| 0xA7 | Month                                        |
| 0xA9 | Year (software-maintained)                   |

- Port `0xA0` updates every 1/1000 sec. Only values with low nibble `0x0`
  appear (`0x00`, `0x10`, etc.).
- Year is not tracked by hardware; port `0xA9` is software-maintained
  (for example, `0x21` for 2021).

### RTC Battery-Backed RAM (NVRAM)

Ports `0xA8` to `0xAF` are used by the Partner’s SET UP program to persist configuration across reboots.

| Port | Purpose                  | Description                                       |
| ---- | ------------------------ | ------------------------------------------------- |
| 0xA8 | Unknown                  | Always `0x00` on real Partner, used as a setup marker in some traces |
| 0xA9 | Year (software-managed)  | Last two digits (e.g. `0x21` for 2021)            |
| 0xAA | Unknown                  | Not yet decoded                                   |
| 0xAB | Terminal type & language | Bitfield described below                          |
| 0xAC | Screen width             | `0x51` = 80 chars, `0x85` = 132 chars             |
| 0xAD | Unknown                  | Not yet decoded                                   |
| 0xAE | Display options          | Bitfield: background, wrap, newline               |
| 0xAF | Keyboard options         | Bitfield: layout, key click, autorepeat           |

### BIOS/SET UP Configuration Details

#### Port 0xAB – Terminal Type and Language

**High nibble (terminal type):**

- `1X`: Partner (emulator: `2X`)
- `2X`: VT52 (emulator: `1X`)
- `0X`: ANSI

**Low nibble (language):**

- `X0`: US ASCII
- `X1`: UK ASCII
- `X2`: Spanish
- `X3`: French
- `X4`: German
- `X5`: Italian
- `X6`: Danish
- `X7`: Swedish
- `X8`: Yugoslav

#### Port 0xAC – Screen Width

- `0x51` = 80 characters
- `0x85` = 132 characters

#### Port 0xAE – Display Behavior

- **Bit 0 (BG):** 0 = normal, 1 = reverse
- **Bit 1 (wrap):** 1 = wrap enabled, 0 = off
- **Bit 2 (newline):** 1 = auto-newline, 0 = off

Examples:

- `0x06`: wrap + newline
- `0x07`: reverse + wrap + newline
- `0x02`: wrap only
- `0x00`: all off

#### Port 0xAF – Keyboard Settings

- **Bit 3 (key click):** 0 = yes, 1 = no
- **Bit 5 (autorepeat):** 0 = yes, 1 = no
- **Bit 7 (layout):** 0 = QWERTY, 1 = QWERTZ

Examples:

- `0x28`: QWERTY, key click ON, autorepeat ON
- `0xA8`: QWERTZ, key click OFF, autorepeat OFF

## Analysis / Interpretation

The MM58167A RTC provides timekeeping and doubles as a simple NVRAM
configuration store. The SET UP program uses ports `0xAB`, `0xAC`, `0xAE`,
and `0xAF` to save BIOS-like settings.

Because there is no true hardware year counter, the system relies on software
to track the year (`0xA9`).

Emulator differences at port `0xA8` may act as a detection mechanism or version signature.

## Solution / Summary

The RTC and NVRAM combo acts as a basic BIOS configuration store:

- Port `0xA9` is used for storing the year manually.
- `0xAB`–`0xAF` configure terminal, display, and keyboard.
- Port `0xA8` may indicate machine type or firmware revision.

These findings are essential for developing accurate emulators or tools that
read or replicate the Partner boot-time state.

## Emulator Notes

- The MM58167 setup/NVRAM area should behave as battery-backed storage.
- A machine reset must not wipe `0xA8..0xAF`; only first-time power-on
  initialization should seed defaults.
- Safe Partner defaults for emulator bring-up are:
  - `0xAB = 0x10` (Partner terminal, US ASCII)
  - `0xAC = 0x85` (132 columns)
  - `0xAE = 0x06` (wrap + newline)
  - `0xAF = 0x28` (QWERTY, click on, autorepeat on)
- If these bytes are left zeroed, firmware and CP/M utilities may fall back to
  degraded or nonsensical display behavior.
- The distributable seed uses `0xAB = 0x08`: ANSI terminal mode in the high
  nibble and the valid Yugoslav character set in the low nibble. Its
  checksum-stamped final byte is `0xAF = 0x4F`. The former invalid language
  value `0xB` translated decimal digits into tiny alternate glyphs `0xD8`
  through `0xE1` in the GDP BIOS.

## Default values

a8 = 240
a9 = 152
aa = 255
ab = 1
ac = 133
ad = 7
ae = 0
af = 87

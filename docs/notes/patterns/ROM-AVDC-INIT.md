# Pattern: AVDC Initialization Sequence (from ROM)

Category: AVDC / ROM  
Date(s): 2021-04-05, 2026-03-20

## Problem / Purpose

Document the low-level AVDC (Advanced Video Display Controller) initialization
sequence used in Iskra Delta Partner WF/G ROM code.

## Findings / Observations

The AVDC is initialized in two stages in system ROM. The routines
`AVDCInit1` and `AVDCInit2` are called at addresses `0x01A2` and `0x01A5`
during early startup.

### AVDCInit1 (ROM address 0x0262)

- Perform master reset by writing `0x00` to port `0x39`
- Delay with three calls to a delay routine
- Clear screen start addresses:
  - `0x3A`, `0x3B` (screen start 1 = 0)
  - `0x3E`, `0x3F` (screen start 2 = 0)
- Write `0x10` to port `0x39` (load IR pointer with value 0)
- Output 10 bytes (IR0–IR9) from table at `0x02AC` to port `0x38` using `otir`

IR table values (address `0x02AC`):

| IR  | Value | Exact decode |
| --- | ----- | ------------ |
| 0   | 0xD0  | double-height/width control enabled, 11 scan lines/row, VSYNC, independent buffer mode |
| 1   | 0x2F  | non-interlaced, equalizing constant = 48 CCLK |
| 2   | 0x0D  | row table off, HSYNC = 4 CCLK, horizontal back porch = 19 CCLK |
| 3   | 0x05  | vertical front porch = 4 lines, vertical back porch = 14 lines |
| 4   | 0x99  | character blink period = 128 fields, 26 active rows |
| 5   | 0x4F  | 80 active characters/row |
| 6   | 0x0A  | cursor first line 0, last line 10 |
| 7   | 0xEA  | VSYNC = 7 lines, cursor blink enabled at 32 fields, underline line 10 |
| 8   | 0x00  | display-buffer first-address low byte |
| 9   | 0x30  | first address = 0, last address = 0x0FFF |

With the 18 MHz/9-dot CMAC setting normally used for this 80-column stage,
CCLK is 2 MHz. These registers produce 112 CCLK/line and 311 lines/field:

```text
Htotal = 2 * (48 + 2*4) = 112 CCLK
Hfront = 112 - 80 - 4 - 19 = 9 CCLK
Vtotal = 26*11 + 4 + 7 + 14 = 311 lines
```

### AVDCInit2 (ROM address 0x0286)

- Enable cursor by writing `0x3D` to port `0x39`
- Cursor position set to (0,0) via ports `0x3D` (low) and `0x3C` (high)
- Display pointer set to `0x1FFF` via helper routine `AVDCNastaviDispAddr`
- Screen filled with spaces (0x20) and attribute `0x00` using ports `0x34` and `0x35`
- Final command `0xBB` to port `0x39` to write from cursor to pointer

### 2026 Clarifications (Validated During Emulator Bring-up)

- AVDC bus behavior is not a flat 0x34..0x3F register file.
- ROM uses a split interface:
  - `0x34` = char buffer latch
  - `0x35` = attr buffer latch
  - `0x38..0x3F` = AVDC control/address register window (offsets 0..7)
- Effective control/address mapping (SCN2674-style):
  - `0x38`: init/data register (IR writes)
  - `0x39`: command/status
  - `0x3A/0x3B`: screen start 1 low/high
  - `0x3C/0x3D`: cursor low/high
  - `0x3E/0x3F`: screen start 2 low/high
- Runtime command traffic observed in GDP CP/M path includes:
  - init/control: `0x00`, `0x10`, `0x1A`, `0x1C`, `0x3D`
- delayed memory operations: `0xAA`, `0xAB`, `0xBB`
  - display-control class: `0x30`, `0x35`
- IR state observed during runtime in current traces:
  - `IR0..IR9 = D0 3E BF 05 99 83 0B EA 00 30`
  - `IR2` high bit set (`0xBF`) indicates row-table mode active in runtime.
  - `IR5 = 0x83` selects 132 active characters, not 131.

With the 24 MHz/8-dot CMAC setting used for 132 columns, CCLK is 3 MHz.
The runtime values produce 190 CCLK/line, including a 15-CCLK horizontal
front porch. The active character raster is exactly `132 * 8 = 1056` dots.

### 2026 Runtime Cross-Check

- The GDP hard-disk integration now validates the final CP/M prompt in AVDC
  VRAM, enters `paket`, receives the serial-side Squid response, observes the
  catalog on the display, and verifies that the cursor returns after `A>`.
- Serial diagnostics remain useful for separating CPU boot progress from
  display rendering, but the AVDC prompt path is now a regression gate.

## Analysis / Interpretation

Initialization configures AVDC text timing and cursor parameters, clears text
memory through AVDC commands, and prepares the controller for runtime
command-driven buffer operations.

These routines are critical for understanding how screen memory and rendering
are handled at the hardware level during early Partner boot.

## Solution / Summary

This ROM-level AVDC initialization sequence includes:

- Reset logic and screen base config
- Full IR0–IR9 setup for display geometry/timing
- Cursor activation and screen memory clear via `0xBB`
- Command-driven char/attr buffer writes are required for accurate emulation

This sequence is essential for emulating or diagnosing early Partner boot behavior.

For every IR, screen-start/split/interlace behavior, and exact delayed-command
cycle cases, see [GDP-AVDC-CMAC-TIMING.md](GDP-AVDC-CMAC-TIMING.md).

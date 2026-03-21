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

| IR  | Value | Description                                            |
| --- | ----- | ------------------------------------------------------ |
| 0   | 0xD0  | double width, 10 scanlines, csync, independent buffers |
| 1   | 0x2F  | no interlace, equalizing const                         |
| 2   | 0x0D  | sync width/back porch, row-table bit off in init      |
| 3   | 0x05  | vertical back porch                                    |
| 4   | 0x99  | blink enabled, 25 rows                                 |
| 5   | 0x4F  | 79 characters per row                                  |
| 6   | 0x0A  | cursor scan lines                                      |
| 7   | 0xEA  | vsync, blink enabled, rate, underline position         |
| 8   | 0x00  | display buffer LSB                                     |
| 9   | 0x30  | buffer range (last address = 8191)                     |

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

### 2026 Runtime Cross-Check (Boot vs Display)

- GDP bootstrap text path can progress to CP/M loader strings even when AVDC final text rendering is still imperfect.
- In traces this appears as:
  - ROM/loader text seen on serial-side diagnostics (`Boot V`, `CP/M V3.0 Loader`, `61K TPA`)
  - AVDC still needing fuller command interpretation for final interactive prompt display.
- Diagnostic implication:
  - a missing visible prompt is not always a boot failure; it can be AVDC render-path incompleteness.

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

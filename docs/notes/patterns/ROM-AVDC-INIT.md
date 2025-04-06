# Pattern: AVDC Initialization Sequence (from ROM)

Category: AVDC / ROM  
Date(s): 2021-04-05

## Problem / Purpose

Understand the low-level initialization sequence of the AVDC (Advanced Video Display Controller) used in the Iskra Delta Partner WF/G, as implemented in ROM.

## Findings / Observations

The AVDC is initialized in two stages within the system ROM. The routines `AVDCInit1` and `AVDCInit2` are called at addresses `0x01A2` and `0x01A5` respectively during early system startup.

### AVDCInit1 (ROM address 0x0262)

- Master reset by writing `0x00` to port `0x39`
- Delay via three calls to a delay routine
- Clear screen start addresses:
  - `0x3E`, `0x3F` (screen start 1 = 0)
  - `0x3A`, `0x3B` (screen start 2 = 0)
- Write `0x10` to port `0x39` (reset IR pointer)
- Output 10 bytes (IR0–IR9) from table at `0x02AC` to port `0x38` using `otir`

IR table values (address `0x02AC`):

| IR  | Value | Description                                            |
| --- | ----- | ------------------------------------------------------ |
| 0   | 0xD0  | double width, 10 scanlines, csync, independent buffers |
| 1   | 0x2F  | no interlace, equalizing const                         |
| 2   | 0x0D  | sync width and back porch                              |
| 3   | 0x05  | vertical back porch                                    |
| 4   | 0x99  | blink enabled, 25 rows                                 |
| 5   | 0x4F  | 79 characters per row                                  |
| 6   | 0x0A  | cursor scan lines                                      |
| 7   | 0xEA  | vsync, blink enabled, rate, underline position         |
| 8   | 0x00  | display buffer LSB                                     |
| 9   | 0x30  | buffer range (last address = 8191)                     |

### AVDCInit2 (ROM address 0x0286)

- Enables cursor by writing `0x3D` to port `0x39`
- Cursor position set to (0,0) via ports `0x3D` and `0x3C`
- Display pointer set to `0x1FFF` via helper routine `AVDCNastaviDispAddr`
- Screen filled with spaces (0x20) and attribute `0x00` using ports `0x34` and `0x35`
- Final command `0xBB` to port `0x39` to write from cursor to pointer

## Analysis / Interpretation

The initialization configures AVDC to an 80x25 text mode with defined sync and cursor parameters. It clears and sets up the framebuffer, activates cursor, and prepares the display.

These routines are critical to understanding how screen memory and rendering are handled at a hardware level in early Partner boot ROM.

## Solution / Summary

This ROM-level AVDC initialization sequence includes:

- Reset logic and screen base config
- Full IR0–IR9 setup for display geometry and timing
- Cursor activation and screen memory clear

It’s essential for emulating or diagnosing early Partner boot behavior.

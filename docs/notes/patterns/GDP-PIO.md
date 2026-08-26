# Pattern: Partner Video Hardware and Thomson Display Initialization

Category: Display / BIOS / PIO / Monitor Timing  
Date(s): 2021-04-06, 2021-05-28, 2026-03-20

## Problem / Purpose

Document the Iskra Delta Partner video subsystem, including monitor assumptions,
and summarize how BIOS initializes the Thomson graphics path. Clarify how the
GDP-board PIO and display controllers are configured to produce valid video.

## Findings / Observations

### Monitor Hardware

- Vendor: **Matsushita**
- Model: **M-12021NB**, 12" green phosphor CRT monitor
- Vertical Refresh Rate: **60 Hz**
- Horizontal Sync Frequency: **15.75 kHz**
- These rates are in the **525-line/60 Hz** timing family, not 625-line PAL.
  The emulator's 624-line composition canvas must not be used as the physical
  monitor scan total.

Do not infer AVDC timing from the output canvas. The ROM's `IR4 = 0x99`
decodes to 26 active rows and `IR0 = 0xD0` decodes to 11 scan lines per
character row. The `1056x624` framebuffer is the emulator's common monitor
composition space, not a literal decode of those two registers.

### BIOS Code Analysis – PIO and Display Setup

#### `pio_init` — initialize PIO for display and input

Key observations from ROM and emulator tracing:

- Startup programs the GDP-board local PIO (`0x30..0x33`) before AVDC/GDP initialization.
- GDP (EF9367) and AVDC (SCN2674) are coupled in output composition through board glue,
  but they use separate memory/state.
- Clearing GDP graphics does not clear AVDC text memory, and vice versa.

### Display Composition Model (Validated)

- Full visible raster: **1056 x 624**
- GDP graphics plane (EF9367): **1024 x 512**, centered in the full raster.
- AVDC text plane (SCN2674): logical **132 x 26** characters in the full raster space
  (8 x 12 character cells for geometry/composition).
- Final monitor output is a composition of both planes.

### EF9367 Runtime Mode Changes (Observed in Probe)

- During GDP boot text drawing, EF9367 control registers are not static.
- Typical sequence observed:
  - `CR1 = 0x03` (stable through bootstrap paths)
  - `CHSZ` switches between `0x03`, `0x21`, `0x22` (and later `0x32/0x33` in graphic-heavy code)
  - `CR2` briefly toggles `0x08` during side-annotation/rotated text phases, then returns to `0x00`
  - later drawing code also uses `CR2 = 0x10` in non-boot loops.
- Practical implication:
  - EF text rendering must honor **runtime magnification** (`CHSZ`) and **orientation modes** (`CR2`) or bootstrap visuals will diverge from hardware behavior.

### Operational Notes for Emulation

- The GDP screen should behave as a monitor, not as a terminal widget.
- No synthetic cursor should be shown before AVDC enables cursor via command flow.
- AVDC should be driven by its command and buffer protocol (char/attr latches and delayed commands), not by direct terminal text assumptions.
- GDP PIO Port B is also part of raster timing:
  - bit 7 selects the 18 MHz or 24 MHz SCB2675 dot-clock path;
  - bits 6:5 drive `C1:C0`, selecting 10, 7, 8, or 9 dots/character;
  - CCLK is the selected dot clock divided by that character width.
- Attribute bit 2 selects the 128-by-16 user-defined character RAM. Attribute
  bit 3 drives scan-line-latched CMAC dot stretch; it is not a PIO bit and is
  not a per-character effect.

The complete port map, clock equations, and command-cycle cases are in
[GDP-AVDC-CMAC-TIMING.md](GDP-AVDC-CMAC-TIMING.md).

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
- Likely matches **625-line PAL** standard, though only ~512 lines are used for display

This aligns with AVDC IR configuration seen elsewhere (`IR4 = 25` rows,
`IR0 = 10` scanlines per character, etc.).

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

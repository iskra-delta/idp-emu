# Pattern: Partner Video Hardware and Thomson Display Initialization

Category: Display / BIOS / PIO / Monitor Timing  
Date(s): 2021-04-06, 2021-05-28

## Problem / Purpose

Document the video subsystem of the Iskra Delta Partner, including hardware assumptions about the connected monitor, and analyze the BIOS routines used to initialize the Thomson graphics system. Understand how the PIO and display controller are set up to produce usable video.

## Findings / Observations

### Monitor Hardware

- Vendor: **Matsushita**
- Model: **M-12021NB**, 12" green phosphor CRT monitor
- Vertical Refresh Rate: **60 Hz**
- Horizontal Sync Frequency: **15.75 kHz**
- Likely matches **625-line PAL** standard, though only ~512 lines are used for display

This matches the AVDC IR register configuration seen elsewhere (IR4 = 25 rows, IR0 = 10 scanlines per char, etc.).

### BIOS Code Analysis – PIO and Display Setup

#### `pio_init` — Initialize PIO for display and input

...todo...

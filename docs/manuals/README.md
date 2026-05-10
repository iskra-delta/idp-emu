# Manuals Index (SCN2674 / SCB2675)

This folder contains original Partner manuals plus externally sourced chip documentation used for AVDC/CMAC behavior analysis.

## Newly added internet sources

1. `Signetics_1986_Microprocessor.pdf`
- Source: `https://bitsavers.trailing-edge.com/components/signetics/_dataBooks/1986_Signetics_Microprocessor.pdf`
- Contains both:
  - `SCN2674` Advanced Video Display Controller
  - `SCB2675` Color/Monochrome Attributes Controller

2. `Philips_1987_IC18_Microprocessors_and_Peripherals.pdf`
- Source: `https://ftpmirror.infania.net/sites/bitsavers/components/philips/_dataBooks/1987_IC18_Philips_Microprocessors_and_Peripherals.pdf`
- Includes refreshed `SCN2674`/`SCB2675` family documentation.

3. `Signetics_SCN2674_Product_Spec.pdf`
- Source: `https://datasheet4u.com/pdf/524408/SCN2674.pdf`
- Standalone SCN2674 product specification scan.

## Extracted focused PDFs (from Signetics 1986 book)

1. `Signetics_1986_SCN2674_AVDC_pp134-165.pdf`
- Extracted from pages 134..165
- AVDC programming model, display control timing, cursor/pointer commands.

2. `Signetics_1986_SCB2675_CMAC_pp166-176.pdf`
- Extracted from pages 166..176
- CMAC pin behavior including:
  - `DOTS` (dot stretching)
  - `DOTM`/`ADOTM` (dot width control)
  - cursor and attribute pipeline timing
  - dots-per-character timing behavior.

3. `Signetics_1986_SCB2675T_Turbo_CMAC_pp177-187.pdf`
- Extracted from pages 177..187
- Turbo variant; useful for cross-checking CMAC behavior and terminology.

## Notes for emulator work

- The extracted SCB2675 section includes explicit text and timing diagrams for:
  - dot stretching
  - dot width modulation
  - cursor integration with attribute logic.
- These docs are the primary references to model Partner-specific SCB-side font/render quirks on top of SCN2674 state.

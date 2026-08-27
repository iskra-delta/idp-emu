# Pattern: GDP (EF9367) Programming Model

Category: Display / EF9367 / GDP Board Glue  
Date(s): 2026-03-20 to 2026-03-21

## Problem / Purpose

Document how the Partner GDP graphics path behaves at the hardware level and
define emulation rules that match ROM behavior.

## Findings / Observations

- The GDP graphics engine is Thomson EF9367-based.
- Two EF display modes are used on Partner:
  - `1024x512` logical
  - `1024x256` logical (displayed as doubled vertical pixels to fill the same monitor height)
- Mode select is provided by GDP-board control (PIO-driven), using port `0x30`
  output bits 3 and 4:
  - `11` -> `1024x512`
  - `00` -> `1024x256`
  - mixed states are transitional/invalid and should not force random mode flips
- Port `0x30` is GDP local PIO Port A:
  - bit0=`RBNK`, bit1=`WBNK`, bit2=`XORM`, bit3=`FM0`, bit4=`FM1`,
    bit7=`SCRLM`. Bits 5 and 6 are not EF/AVDC interrupt status; EF `/IRQ`
    and conditioned AVDC `/IRQ` drive the PIO `ASTB` and `BSTB` pins.
- EF coordinate origin is bottom-left in logical drawing space:
  - `x` grows to the right
  - `y` grows upward
  - logical line `0` is the bottom line
- EF framebuffer memory per page is 64 KiB (`1024 * 512 / 8`).
- At `1024x256`, only half of the 64 KiB page (32 KiB worth of logical lines) is
  actively used for visible source data at any one time; the rest may contain stale data.
- Scroll is always a window over the full 64 KiB page memory.
  - In `1024x512`, wrap is visible: lines that move past the top reappear at the bottom.
  - In `1024x256`, wrap over 64 KiB still exists electrically, but is usually not visible
    because half of the page is outside the active low-resolution source region.
- ROM startup normally uses page 0; page switching exists in hardware but is not
  used by current boot paths.
- GDP external scroll latch (`port 0x36`) is board-level glue, not an EF internal register.
- Scroll is in **physical scanlines**.
  - `scroll=0`: output starts at raster row 0
  - `scroll=1`: output starts at raster row 1 (screen appears moved up by one line)
- Partner startup behavior suggests the board-level scroll latch affects where GDP
  writes land in page memory, not only final scanout.
  - This matches the ROM newline path: it updates `0x36`, then issues a GDP
    clear-line string, then prints the next line.
  - In practice, this produces the expected stacked startup layout
    (`banner`, then `boot version`, then `TESTING MEMORY ...`) without the
    lines erasing each other.
- EF command `0x05` resets both X and Y. Command `0x0D` resets only X, and
  command `0x0E` resets only Y. The Partner ROM's use of `0x05` therefore
  returns the pen to the full coordinate origin.

## Emulation Rules (Practical)

- Keep EF drawing bottom-origin inside the chip model.
- Keep framebuffer scanout top-origin in renderer space.
- Model GDP `0x36` scrolling as board-level page-memory line offset for GDP
  writes/line clears, rather than only as a post-render viewport shift.
- In `1024x512` mode:
  - scanout wraps across 512 lines.
- In `1024x256` mode:
  - each logical line is doubled vertically on write/render;
  - do not force visible wrap into inactive half-lines as if the full 512-line source were active.
- Do not silently clear EF pages at startup unless commanded; startup state can be dirty.
- GDP clear screen clears entire GDP page 64KB regardless of resolution, 
  but only one page,not both, only the write page
- Treat GDP PIO outputs as real control lines that gate mode/behavior.
- In `XORM`, every plotted position toggles even when EF eraser is selected;
  pen/eraser-up still prevents a plot.
- READY timing is workload-dependent at the 1.5 MHz EF clock. Vectors take
  four setup clocks plus one clock per component dot, characters take
  `48*P*Q` clocks, and clear/scan commands run to the next VB falling edge and
  then occupy one field in non-interlaced format or two fields when interlaced.
  Exact phase conversion and cases
  are documented in
  [GDP-AVDC-CMAC-TIMING.md](GDP-AVDC-CMAC-TIMING.md).

## Current Emulator Status

- EF mode select from GDP PIO bits 3/4 is implemented.
- EF low-resolution vertical doubling is implemented in pixel write path.
- Scroll latch `0x36` is currently modeled as a board-level write offset that
  matches Partner ROM startup behavior more closely than a pure scanout offset.
- EF read/write bank selection from GDP PIO (`RBNK`/`WRNK`) is implemented.
- EF command `0x0F` requests the next free graphics-memory cycle. READY stays
  low while the request waits for `ALL` to become high and throughout the
  active `MW` cycle. At completion, Partner board glue latches the addressed
  write-bank pixel and returns its active-low sense on port `0x36` bit 7.
- GDP and AVDC planes are blended additively in the renderer (overlap appears brighter).
- Renderer uses centered GDP plane composition inside full display raster.
- Remaining work is mostly visual parity/detail tuning rather than basic GDP command-path bring-up.

## Notes

- Partner output is composited from separate planes (GDP graphics + AVDC text path).
- Clearing one plane does not imply clearing the other.
- Pixel intensity is additive in composition terms; a pixel driven by both AVDC
  and GDP appears brighter than a pixel driven by only one plane.
- The emulator composes both planes in a `1056x624` monitor canvas. This is a
  render-space convention, not a fixed AVDC register geometry. The observed
  132-column runtime mode uses 132 characters by 8 dots; the ROM's
  `IR0=0xD0` selects 11 AVDC scan lines per active character row.
- The GDP raster is smaller (`1024x512`) and is centered inside the full
  `1056x624` monitor raster.

## ToDo

- Validate GDP scroll phase against hardware captures for both resolutions:
  - `1024x512`: wrap at 512 lines must be visibly correct.
  - `1024x256`: verify practical non-wrapping visible behavior against ROM traces.
- Confirm whether Partner GDP really applies scroll only to scanout, or whether
  board glue also biases GDP write addresses into video RAM. The current
  emulator uses the latter model because it matches the ROM boot banner and
  per-line clear behavior much better.
- Confirm GDP bank wiring semantics on real hardware. The emulator now has a
  deterministic page-1 diagnostic proving that `RBNK` selects scanout and
  `WBNK` selects the draw/clear destination under scroll.
- Confirm additive compositing brightness curve against real monitor photos
  (current blend is intentionally simple and may need tuning).
- Audit remaining EF command semantics that can affect rotation/magnification edge cases:
  - runtime `CHSZ` changes,
  - `CR2` orientation transitions,
  - cursor/pen advance under rotated text modes.
- Continue hardware validation of AVDC setup-window edge cases and mixed-plane
  cursor/blink presentation. Core row-table, split, interlace, delayed-command,
  and CMAC divider timing now have focused regression coverage.

## Reading a GDP pixel

The Partner has no CPU-visible byte or word path from GDP memory. The card
implements the EF9367 datasheet's suggested external direct-access register as
a one-bit latch. Port `0x36` therefore has direction-dependent board glue:

| Access | Function |
| --- | --- |
| Read D7 | active-low GDP pixel latched by the last completed command `0x0F` |
| Read D4 | AVDC `RESTRICT` timing latch |
| Write | GDP scroll latch |

The original Partner `CGRAF.COM` uses the following protocol. Status is read
at `0x2F`, rather than `0x20`, so polling does not acknowledge pending EF
interrupt latches.

```asm
; Input: HL = X, DE = Y
; Output: A = 80h for a set pixel, 00h for a clear pixel

gdp_read_pixel:
        call    gdp_wait

        ld      a,l
        out     (29h),a         ; X low
        ld      a,h
        out     (28h),a         ; X high nibble
        ld      a,e
        out     (2bh),a         ; Y low
        ld      a,d
        out     (2ah),a         ; Y high nibble

        ld      a,0fh
        out     (20h),a         ; request next free memory cycle
        call    gdp_wait         ; wait until MW cycle and LOAD have completed

        in      a,(36h)
        cpl                     ; D7 is active-low
        and     80h
        ret

gdp_wait:
        in      a,(2fh)         ; non-destructive STATUS alias
        and     04h             ; READY
        jr      z,gdp_wait
        ret
```

Direct access follows `WBNK`, the drawing page. To inspect the page currently
being displayed, first make `WBNK` equal `RBNK`, taking care not to disturb the
other PIO-A control bits. The current format and scroll mapping also apply.

This is not a fast framebuffer-copy interface. During active display the
request may wait through the remainder of a 64-CK `ALL`-low memory window,
which is at most about 42.7 microseconds at the Partner's 1.5 MHz EF clock.
Reading eight pixels requires eight command/request/latch/read sequences. For
sprite restoration, use `XORM` and redraw the same shape, or maintain a CPU
shadow bitmap when the actual background bits are needed.

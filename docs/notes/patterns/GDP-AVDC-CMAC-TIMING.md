# Partner GDP, AVDC, and CMAC Timing Contract

Category: Display / EF9367 / SCN2674 / SCB2675 / GDP Board Glue  
Date: 2026-08-26

## Purpose and Source Priority

This is the detailed timing and register reference for the Partner GDP video
board. It records which behavior comes from a chip specification, which comes
from Partner wiring, and which is an emulator choice. That distinction matters:
the EF9367, SCN2674, and SCB2675 use different clock domains, while several
visible controls are implemented in board glue rather than in either display
controller.

Use sources in this order when resolving a disagreement:

1. Partner board schematics: `docs/vendor/Graficna_kartica.pdf` and
   `docs/vendor/IDC-Partner GDP_nacrt.pdf`.
2. SCN2674 specification:
   `docs/vendor/Signetics_1986_SCN2674_AVDC_pp134-165.pdf`.
3. SCB2675 specification:
   `docs/vendor/Signetics_1986_SCB2675_CMAC_pp166-176.pdf`.
4. SGS-Thomson EF9367 specification, December 1988:
   <https://wolfgangrobel.de/sbc/files_vme/EF9367.pdf>.
5. Partner ROM traces and focused emulator tests.

The implementation references are:

- `lib/chipsex/thomson/ef9367.h`
- `lib/chipsex/signetics/scn2674.h`
- `src/partner_gdp.cpp`
- `tests/test_ef9367.cpp`
- `tests/test_scn2674.cpp`
- `tests/test_partner_gdp_memacc.cpp`

## Clock Domains

The emulator master tick is one 4 MHz Z80 T-state, or 250 ns.

### EF9367 GDP clock

The Partner derives EF9367 `CK` by dividing the 24 MHz video crystal by 16:

```text
EF CK = 1.5 MHz
1 EF CK = 666.667 ns = 8/3 emulator master ticks
```

The emulator retains the fractional phase. For an operation requiring `N` EF
clocks and beginning at phase `p`, where `p` is 0 through 7 eighths of an EF
clock already elapsed, its remaining 4 MHz BUSY count is:

```text
master_ticks = ceil((8*N - p) / 3)
```

Do not replace this with `ceil(8*N/3)` per command: doing so discards phase
and introduces cumulative timing drift.

The EF raster is 96 CK per monitor line. In the modeled 525-line mode one
field is 262.5 lines, or 25,200 CK. The raster phase used for long commands is
therefore modulo 25,200 CK.

### AVDC and CMAC clocks

PIO Port B selects the SCB2675 dot clock and dot-clock divider. The CMAC emits
the character clock used by the SCN2674:

```text
CCLK = DCLK / dots_per_character
```

For the Partner's SCB2675B:

| PIO B bits 6:5 (`C1:C0`) | Dots/character | CCLK high/low DCLKs |
| --- | ---: | --- |
| `00` | 10 | 5/5 |
| `01` | 7 | 3/4 |
| `10` | 8 | 4/4 |
| `11` | 9 | 4/5 |

PIO B bit 7 selects `DCLK`: zero selects 18 MHz and one selects 24 MHz.
The emulator advances both DCLK and CCLK with fractional accumulators relative
to the 4 MHz master tick. PIO writes change the live clock; they are not merely
renderer width hints.

The complete PIO-B wiring visible on Partner schematic sheets 10 and 14 is:

| PIO B bit | Board net | CMAC/clock destination |
| ---: | --- | --- |
| 0 | `CA0` | no CMAC or DCLK connection shown; not consumed by the emulator |
| 1 | `CA1` | CMAC `CMODE` cursor-mode input |
| 2 | `CA2` | CMAC `M/C` color/monochrome select |
| 3 | `CA3` | CMAC `ABLUEF/ABLANK` |
| 4 | `CA4` | CMAC `AGREENF/BKGND` |
| 5 | `CA5` | CMAC `C0` divider input |
| 6 | `CA6` | CMAC `C1` divider input |
| 7 | `CA7` | 18/24 MHz DCLK multiplexer select |

This is why `ATTD3`, not PIO B bit 0, controls dot stretch. The other
per-character CMAC attributes come from the attribute RAM.

Typical useful combinations are:

| Use | PB7 | `C1:C0` | DCLK | CCLK | Active-dot width |
| --- | ---: | --- | ---: | ---: | ---: |
| 80-column | 0 | `11` | 18 MHz | 2 MHz | 80 x 9 = 720 dots, represented as 960 24-MHz raster periods |
| 132-column | 1 | `10` | 24 MHz | 3 MHz | 132 x 8 = 1056 dots |

The first row is a common ROM configuration, not a restriction: all four
divider settings and both clocks are live.

## EF9367 Register and Board Interface

The EF9367 occupies `0x20..0x2F`. Reserved register reads return `0xFF`.

| Port | Read | Write |
| --- | --- | --- |
| `0x20` | STATUS, clears latched EF interrupt flags on real hardware | CMD |
| `0x21` | CTRL1 | CTRL1 |
| `0x22` | CTRL2 | CTRL2 |
| `0x23` | CSIZE | CSIZE |
| `0x25` | DELTAX | DELTAX |
| `0x27` | DELTAY | DELTAY |
| `0x28`, `0x29` | X upper nibble, X low byte | same |
| `0x2A`, `0x2B` | Y upper nibble, Y low byte | same |
| `0x2C`, `0x2D` | light-pen X/Y; reading either clears the hit indication | read-only |
| `0x2F` | non-destructive STATUS on the chip | reserved |

STATUS bit 0 is light-pen availability, bit 1 is vertical blank, bit 2 is
READY, and bit 3 is coordinate overflow. Bits 4, 5, and 6 latch enabled
light-pen, vertical-blank, and READY interrupts; bit 7 reports their combined
interrupt state. Reading `0x20` acknowledges those latches, while `0x2F` is a
non-destructive status read. READY is low while the modeled command BUSY count
is nonzero. Command `0x0F` also holds READY low while its memory request is
pending and during the complete `MW` cycle. Software must not issue another
command until READY is high.

CTRL1 bits 0 and 1 select down/up and pen/eraser. CTRL2 bits 1:0 select line
style, bit 2 selects tilted characters, and bit 3 selects vertical character
advance. CSIZE holds `P` in the high nibble and `Q` in the low nibble; a zero
nibble encodes 16, not zero.

PIO Port A provides Partner board controls outside the EF9367:

| Bit | Signal | Meaning |
| ---: | --- | --- |
| 0 | `RBNK` | displayed/read page |
| 1 | `WBNK` | page modified by drawing |
| 2 | `XORM` | toggle plotted memory bits |
| 3, 4 | `FM0`, `FM1` | Partner format controls: `00` = 256 logical lines, `11` = 512; mixed values are transitional |
| 5 | — | no EF status input on Port A |
| 6 | — | no AVDC status input on Port A |
| 7 | `SCRLM` | active-low external scroll enable |

The PIO must first be programmed as an output. After PIO reset, writing its
data register only changes the internal latch and does not drive these board
signals. EF `/IRQ` drives `ASTB`, and conditioned AVDC `/IRQ` drives `BSTB`;
the PIO observes them through its handshake/interrupt pins.

### EF9367 command cycles

The following counts are EF `CK` cycles. Convert them to 4 MHz master ticks
with the phase-aware formula above.

| Command class | EF CK BUSY time in the emulator | Detailed case |
| --- | ---: | --- |
| Register-only/default command | 2 | Models the command synchronizer's maximum two CK. Includes pen/eraser select, up/down, X/Y home, and otherwise unmodeled commands. |
| Standard vector `0x10..0x1F` | `4 + dots` | Two sync CK + two initialization CK + one CK per component dot, including both endpoints. Commands `0x18..0x1F` replace the smaller delta with the larger. |
| Small vector `0x80..0xFF` | `4 + dots` | Same sequence; DELTAX and DELTAY are taken from the packed command fields. |
| Printable character `0x20..0x7F` | `6P * 8Q = 48PQ` | Includes the one-dot inter-character spacing in the six-dot cell. |
| Solid block `0x0A` | `48PQ` | Draws `5P x 8Q`, then advances by the normal `6P` character pitch; timing includes the sixth spacing column. |
| Solid block `0x0B` | `16PQ` | `4P * 4Q`. |
| Clear/reset/scan `0x04`, `0x06`, `0x07`, `0x0C` | wait to next VB falling edge + 25,200 or 50,400 CK | One field in 256-line/non-interlaced format; two fields in 512-line/interlaced format. |
| Direct memory request `0x0F` | wait for next free memory cycle + 1 CK | READY is low while pending and while `MW` is active. The worst normal-display wait is 64 CK. |

For standard vectors, `steps` and `dots` are:

```text
0x10, 0x16 axis vectors: steps = abs(DELTAX)
0x12, 0x14 axis vectors: steps = abs(DELTAY)
0x11, 0x13, 0x15, 0x17: steps = max(abs(DELTAX), abs(DELTAY))
0x18..0x1F: first replace both deltas by max(abs(DELTAX), abs(DELTAY))
dots = steps + 1
busy_CK = steps + 5
```

For small vectors, DELTAX is `(CMD >> 5) & 3` and DELTAY is
`(CMD >> 3) & 3`; the same axis/oblique rule is selected by command bits 2:0.
A zero-length vector consequently takes five CK and plots its origin once.

Examples at phase zero:

| Operation | EF CK | 4 MHz ticks |
| --- | ---: | ---: |
| Zero-length vector | 5 | 14 |
| Oblique vector with `max(dx,dy)=100` | 105 | 280 |
| Character at `P=Q=1` | 48 | 128 |
| `4x4` block at `P=Q=1` | 16 | 43 |
| Clear issued at raster reset | 53,856 | 143,616 |

The clear example consists of 3,456 CK to the modeled VB falling edge at line
36, followed by 50,400 CK of scanning.

Rendering/memory mutation currently happens atomically when the command is
accepted; READY remains low for the duration above. Thus CPU-visible command
latency is modeled, but partially drawn vectors and progressive clear tearing
are not yet exposed.

### Direct graphics-memory access and the port `0x36` pixel latch

In the 525-line timing diagram, each line is 96 EF clocks. The collective
display-memory interval begins after 23 CK and lasts 64 CK. During active
display lines 36 through 243, `ALL` is low over that interval. The same
64-cycle interval is occupied by refresh on vertical-blank lines 10 through
13, 26 through 29, and 248 through 251. Command `0x0F` waits until `ALL` is
high, asserts active-low `MW` for one complete free CK, and raises READY only
after that CK has ended.

The EF9367 provides the address and handshake but no internal pixel-data
register. Partner sheet 7 completes the path externally: IC22 derives `LOAD`
from the `MW` memory cycle, IC1 latches active-low `DOUT`, and IC15 returns its
Q output on CPU D7 when port `0x36` is read. D4 simultaneously carries the
AVDC `RESTRICT` latch. A successful access therefore uses:

```text
set X/Y -> command 0Fh -> poll port 2Fh READY -> read port 36h D7 -> invert D7
```

The latch follows `WBNK` and retains its value until another direct-access
cycle completes. There is no byte-wide GDP-memory path to the Z80.

### EF line patterns and XOR

CTRL2 patterns restart at phase zero for each vector:

| CTRL2 bits 1:0 | Pattern, repeated |
| --- | --- |
| `00` | continuous |
| `01` | 2 on, 2 off |
| `10` | 4 on, 4 off |
| `11` | 10 on, 2 off, 2 on, 2 off |

Pattern choice does not change vector BUSY time; the generator consumes one
CK per component dot whether its write-enable is on or off.

Partner `XORM` toggles every position the GDP attempts to plot. This applies
with either EF pen or eraser selected. Pen/eraser-up still suppresses plotting.
Consequently, repeating the same stroke is an exact involution. Treating an
eraser plot as a no-op in XOR mode is incorrect.

## SCN2674 Board Ports

The Partner does not expose the SCN2674 as a flat twelve-register window:

| Port | Read | Write |
| --- | --- | --- |
| `0x34` | character interface latch | character interface latch |
| `0x35` | attribute interface latch | attribute interface latch |
| `0x36` | active-low GDP pixel D7 plus AVDC `RESTRICT` D4 | GDP external scroll latch |
| `0x37` | `0xFF` | no AVDC effect |
| `0x38` | interrupt register | initialization-register data |
| `0x39` | status register | command register |
| `0x3A`, `0x3B` | screen-start 1 low/high | same |
| `0x3C`, `0x3D` | cursor address low/high | same |
| `0x3E`, `0x3F` | screen-start 2 low/high | same; upper write bits 6/7 enable automatic split 1/2 |

All AVDC display addresses are 14-bit and wrap modulo 16 KiB. The split-enable
bits in screen-start 2 upper are write-only; reads return only address bits
5:0.

The status/interrupt event bits are:

| Bit | Event |
| ---: | --- |
| 4 | vertical blank |
| 3 | line zero |
| 2 | split 1 |
| 1 | delayed command ready/completed |
| 0 | split 2 |

Status read bit 5 is RDFLG: one means no delayed command is active. It becomes
zero when a delayed command is accepted and becomes one on completion. The
interrupt register contains the enabled, latched event bits.

### Partner row-table access restriction

Port `0x36` bit 4 is often mistaken for HSYNC. The GDP schematic shows that
IC26, a 74S374, captures the SCN2674's multiplexed DADD13/LL (`last line`)
value at the falling edge of `BLANK`. The captured value is named `RESTRICT`
and is returned on data bit 4. It is high for the complete last scan line of
each character row and low for the other scan lines.

This circuit implements the SCN2674 data-sheet rule for row-table addressing
in independent/transparent mode: the CPU must not access the AVDC while the
chip is reading the row table. The conservative hardware indication covers
the whole last scan line, including the two-CCLK row-table fetch in its
blanking interval. The real-machine-tested helper is therefore:

```c
while ((AVDC_ACCESS & 0x10) == 0); /* wait until last line */
while ((AVDC_ACCESS & 0x10) != 0); /* wait until following line zero */
```

For an ordinary active row it returns just after the restricted last line, at
line zero of the following character row. At the bottom active row, vertical
blank keeps `BLANK` asserted, so IC26 cannot capture a new low LL value; the
restriction latch remains high through vertical blank and falls at line zero
of the next field. It is an alignment operation, not a persistent bus grant.
AVDC status bit 5 (`RDFLG`) is an independent handshake and must still be
checked before changing a cursor/pointer address or issuing another delayed
command.

For the BIOS-derived 11-scan-line modes, the ideal edge-to-edge budgets are:

| Mode | CCLK | CCLK/line | `RESTRICT=0` safe span (10 lines) | Ordinary-row `RESTRICT=1` span |
| --- | ---: | ---: | ---: | ---: |
| 80 columns, PIO `0x65` | 2 MHz | 112 | 1120 CCLK = 560 us = 2240 Z80 T-states | 112 CCLK = 56 us = 224 T-states |
| 132 columns, PIO `0xC4` | 3 MHz | 190 | 1900 CCLK = 633.333 us = 2533.333 Z80 T-states | 190 CCLK = 63.333 us = 253.333 Z80 T-states |

The CPU samples the falling transition with an I/O instruction and still has
loop/function-return overhead, so software must not treat the tabulated
T-state count as an exact usable instruction budget. For a custom mode with
`L` scan lines per field/character row, the ideal safe interval is
`(L-1) * Htotal / CCLK`; repeat the access wait before that interval expires.

Safe duration and `avdc_wait_access()` latency are different quantities. The
routine does not merely wait while the signal is high. Even if called while
`RESTRICT` is already low, its first loop deliberately waits for the next high
period, and its second loop waits for the subsequent falling edge:

```text
line 0      ...      line 9     line 10             next line 0
RESTRICT=0  safe     RESTRICT=0  RESTRICT=1, unsafe  RESTRICT=0, return
```

Consequently, a call made at a random time normally waits between almost zero
and one complete character-row period. The ordinary-row periods are:

| Mode | One complete 11-scan-line character row |
| --- | ---: |
| 80 columns | 1232 CCLK = 616 us = 2464 Z80 T-states |
| 132 columns | 2090 CCLK = 696.667 us = 2786/2787 Z80 T-states |

The one interval crossing the bottom of the screen also includes the 25-line
vertical blank (`VFP + VSYNC + VBP = 4 + 7 + 14`). From line zero of the last
active row to line zero of the next field it is 36 scan lines: 2.016 ms or
8064 T-states in 80-column mode, and 2.280 ms or 9120 T-states in 132-column
mode. This is still not a wait for a complete screen refresh.

Every `avdc_write_at_pointer()` or `avdc_write_at_cursor()` call in the
supplied library invokes this alignment routine. Successive character writes
are therefore intentionally throttled to roughly one character-row boundary
per character, with the longer vertical-blank interval once per field. This
does not mean the memory command waits for the bottom of the screen: after the
routine returns at line zero, a five-CCLK single-cell command normally updates
display RAM in the next horizontal blank. Scanout shows it later in the same
field if the target position has not yet been scanned, otherwise in the next
field.

The five-CCLK command time must not be divided directly into the complete safe
span. During active display, a single-cell command waits for horizontal blank.
The BIOS modes have 32 blank CCLK per scan line in 80-column mode and 58 in
132-column mode. An infinitely fast command source could therefore fit at most
`floor(32/5) = 6` or `floor(58/5) = 11` single-cell commands in each blank, or
60/110 commands in the ten safe scan lines. Those are AVDC service-capacity
limits, not achievable CPU burst counts.

For an arbitrary character plus attribute, the 4 MHz Z80 must observe READY
and perform three port writes (character latch, attribute latch, and command).
It cannot reliably complete that handshake and start another five-CCLK command
in the same horizontal blank. A tightly bounded cursor-increment burst thus
settles at approximately one completed character per scan line:

| Mode | Repeated CPU write cadence | Ten-line maximum | Conservative C burst |
| --- | ---: | ---: | ---: |
| 80 columns | about 112 CCLK = 224 T-states = 56 us/character | 10 characters | 9 characters |
| 132 columns | about 190 CCLK = 253.333 T-states = 63.333 us/character | 10 characters | 9 characters |

Ten is valid only for a cycle-counted sequence that starts immediately after
the falling `RESTRICT` edge, has the next command pending before each blank,
and performs no AVDC access after detecting completion in line 9. Nine leaves
one complete safe scan line for compiler, call, polling-phase, and interrupt
latency. Interrupts must be disabled or included in the bound. Code must use a
fresh access wait after the burst. The existing per-character convenience
functions do not form such a burst: each performs its own access wait, so they
write only one character per character-row access interval.

One access wait may cover more than one READY wait only when the whole sequence
has a proven upper bound inside that safe interval. The supplied
`avdc_write_addr_at_cursor()` intentionally does this for two five-CCLK
single-cell commands. An arbitrarily long READY poll is not safe: a block
command can cross many scan lines. This is why `avdc_wait_long_command()`
performs a fresh access wait before every status read.

Compatibility is regression-tested against the initialization and access
sequences in the real-Partner-validated
[`idp-games/src/common/avdc.c`](https://github.com/iskra-delta/idp-games/blob/master/src/common/avdc.c),
including both BIOS-derived init strings, both PIO clock/width values, the
high-to-low access wait, READY timing, and the two-byte row-table write.

### All initialization registers

The initialization pointer resets to IR0. Command `0x10 | n`, for `n=0..14`,
selects an IR; each data write then advances the pointer until it reaches IR14,
where it remains.

All count encodings below are decoded values, not raw bit-field values:

| IR | Bits | Meaning and exact decode |
| ---: | --- | --- |
| 0 | 7 | Enable per-row double height/width control. SSR1 upper bits 7:6 are copied into IR14 bits 7:6 when enabled. |
| 0 | 6:3 | Non-interlaced lines/character row = field + 1. In interlace this is lines/field/row; a full two-field character row has twice that many lines. Raw `1111` is undefined by the data sheet. |
| 0 | 2 | 0 selects VSYNC; 1 selects RS-170 composite sync with equalizing and serration pulses. |
| 0 | 1:0 | buffer mode: independent, transparent, shared, row buffer. |
| 1 | 7 | interlace enable. |
| 1 | 6:0 | equalizing constant `EC = field + 1` CCLK. Horizontal total is `2 * (EC + 2*HSYNC)`. |
| 2 | 7 | row-table addressing; a change takes effect at the next character row. |
| 2 | 6:3 | HSYNC width = `2 * (field + 1)` CCLK. |
| 2 | 2:0 | horizontal back porch: raw zero is not allowed; otherwise `4*field - 1` CCLK, giving 3, 7, 11, 15, 19, 23, or 27 CCLK. |
| 3 | 7:5 | vertical front porch = `4 * (field + 1)` scan lines. |
| 3 | 4:0 | vertical back porch = `2 * (field + 2)` scan lines. |
| 4 | 7 | character blink period: 0 = 64 fields, 1 = 128 fields, 50% duty. |
| 4 | 6:0 | active character rows = `field + 1`. |
| 5 | 7:0 | active characters/row = `field + 1`. |
| 6 | 7:4 | first cursor scan line. |
| 6 | 3:0 | last cursor scan line; first must be less than last. |
| 7 | 7:6 | VSYNC width map: `00`=3, `01`=1, `10`=5, `11`=7 scan lines. |
| 7 | 5 | cursor blink enable. |
| 7 | 4 | cursor period: 0 = 32 fields, 1 = 64 fields, 50% duty. |
| 7 | 3:0 | underline scan-line position. |
| 8, 9 | IR8 + IR9 bits 3:0 | display-buffer first address, 12-bit. |
| 9 | 7:4 | display-buffer last boundary. Decoded last address is `((field + 1)*1024)-1`. |
| 10, 11 | IR10 + IR11 bits 5:0 | display pointer address, 14-bit. |
| 11 | 6 | force line-address zero on the bottom partial row during scroll up. |
| 11 | 7 | force line-address zero on the top partial row during scroll down. |
| 12 | 7 | enable scroll start. |
| 12 | 6:0 | split register 1 / first scrolling row. |
| 13 | 7 | enable scroll end; valid scrolling requires IR12 bit 7 too. |
| 13 | 6:0 | split register 2 / last scrolling row. |
| 14 | 7:6 | double mode at split 1, or current row mode when IR0 bit 7 is set. |
| 14 | 5:4 | double mode at split 2 when IR0 bit 7 is clear. |
| 14 | 3:0 | literal scan-line scroll offset, 0 through 15; it is not count-minus-one encoded. |

Double mode encodings are `00` normal, `01` double-width, `10` double-width
and double-height top, and `11` double-width and double-height bottom. The
AVDC continues to fetch single-width character positions in every CCLK; in
double width the CMAC ignores the second character's data.

Horizontal timing is derived as:

```text
Htotal = 2 * (EC + 2*HSYNC)
Hfront = Htotal - active_characters - HSYNC - Hback
line_frequency = CCLK / Htotal
```

Vertical timing for a non-interlaced field is:

```text
Vactive = rows_per_screen * scanlines_per_field_row
Vtotal = Vactive + Vfront + VSYNC + Vback
field_frequency = line_frequency / Vtotal
```

The CMAC delays video/BLANK by three CCLK relative to AVDC sync. Therefore the
physical CMAC video front porch is three CCLK shorter, and its back porch three
CCLK longer, than the values at the AVDC pins.

### Screen starts, wraparound, row tables, splits, and scrolling

At the first scan line of a field, screen-start 1 is copied to the row-start
register and memory-address counter. The address increments once per active
character CCLK. Every subsequent scan line of that character row reloads the
same row-start address. At the end of the final scan line, the address after
the last active character becomes the next row's start.

When the address reaches the IR9 last boundary, the following address is the
IR8/IR9 first address. Data outside this overwrite/wrap range can still be
displayed via screen starts and splits.

Writing either half of screen-start 1 during active display does not tear the
current row. The new complete address is loaded at the next character-row
boundary. A write outside active display takes effect immediately. Software
must restore the field origin before the next field if it uses this mechanism
for a CPU-controlled split.

Screen-start 2 has two roles:

- With upper bit 6 set, it replaces the row start at split 1.
- With upper bit 7 set, it replaces the row start after split 2, or after the
  partial row when scrolling.
- In row-table mode, it points to the table instead.

A row-table entry is two bytes, low address then upper address. It is fetched
during the blank before a new row and advances screen-start 2 by two. When IR0
bit 7 is set, entry bits 15:14 also supply the row's double mode. The two fetch
CCLKs own the display bus and delay any competing AVDC memory command.

With both IR12 bit 7 and IR13 bit 7 set, IR14 bits 3:0 offset the line address
inside the scrolling region. The first and last rows become complementary
partial rows so the total active scan-line count stays constant. IR11 bits 7
and 6 may force the corresponding partial row's line address to zero.

### Interlace cases

IR1 bit 7 alternates even and odd fields and asserts the ODD output during the
odd field. The sync generator offsets odd-field VSYNC by half a horizontal
line and emits the pre-equalizing, serrated broad-sync, and post-equalizing
pulses selected by IR0 bit 2.

There are two character-generator connections:

- **Interlaced sync only:** LA0..LA3 supply the same ascending line numbers in
  both fields. The same character information is repeated, improving
  readability.
- **Interlaced sync and video:** ODD is used as the least-significant character
  line bit and LA0..LA2 provide the remaining bits. The emulator line address
  is `2*field_scan + odd`, interleaving even and odd glyph rows and doubling
  full-frame character density.

The emulator represents the odd field with one additional whole raster-line
slot, equivalent over two fields to the two half-line porch additions. This
preserves the alternating half-line phase without requiring a half-CCLK
internal coordinate.

### AVDC command timing

Immediate command families do not set RDFLG busy:

| Command | Effect and boundary |
| --- | --- |
| `0x00` | master reset immediately; IR pointer returns to IR0 and display/cursor/graphics are disabled. |
| `0x10..0x1E` | select IR0..IR14 immediately. |
| `0x22`, `0x23` | disable/enable graphics; change is latched at the next character row. |
| `0x28`, `0x2C` | display off immediately; `0x2C` also floats the DADD bus. |
| `0x29` | display on at the next scan line. |
| `0x2D` | display on at the next field. |
| `0x30`, `0x31` | cursor off/on immediately. These controls may be combined with display/graphics controls. |
| `0x40 | mask` | reset selected interrupt/status event latches. |
| `0x60 | mask` | enable selected interrupt sources. |
| `0x80 | mask` | disable selected interrupt sources. |

Delayed memory commands operate in CCLK units:

| Command | Operation | Work once eligible |
| --- | --- | ---: |
| `0xA2` | write interface latches at pointer | 5 CCLK |
| `0xA4` | read interface latches at pointer | 5 CCLK |
| `0xA9` | increment cursor modulo 16 KiB | 3 CCLK, immediately, independent of blank |
| `0xAA` | write at cursor | 5 CCLK |
| `0xAB` | write at cursor, then increment | 5 CCLK |
| `0xAC` | read at cursor | 5 CCLK |
| `0xAD` | read at cursor, then increment | 5 CCLK |
| `0xBB` | fill cursor through pointer, inclusive | 2 blank CCLK per cell |
| `0xBD` | read cursor through pointer, inclusive | 2 blank CCLK per cell; final cell remains in the interface latches |

For a five-CCLK single-cell command:

- With display off or in vertical blank, execution begins immediately.
- In active display it waits for a horizontal blank with at least five CCLK
  remaining.
- If execution is suspended by active display, it resumes in the next blank.
- If the blank precedes a row-table row, the two row-table fetch clocks have
  priority and do not count toward the command.

Block commands execute as many cells as blank time permits, one cell every two
CCLK, pause during active display, and resume in a later horizontal or vertical
blank. Their range is inclusive. Cursor-to-pointer distance wraps modulo
16 KiB. A second delayed command issued before RDFLG returns to one is invalid;
the emulator retains the first command.

The convenience `scn2674_read()` and `scn2674_write()` calls perform no hidden
clock advancement. In the full Partner machine, time advances through the
regular 4 MHz machine tick, and CCLK is derived from the selected DCLK/divider.
This separation is intentional and prevents register polling itself from
inventing video time.

The SCN2674 data sheet also imposes programming setup windows:

| Parameter change | Latest specified time |
| --- | --- |
| cursor first/last line, underline | at least two CCLK before occurrence |
| double mode, lines-to-scroll | before the affected split row |
| blink rate | effective within one field |
| split 1/2 | before line zero of the desired row |
| rows/screen | during vertical blank only |
| vertical front porch | before its first line |
| vertical back porch | before the fourth line after VSYNC |
| screen-start 1, row-table enable | before horizontal blank of the prior row's last scan line |

Software should observe these windows. The emulator explicitly models the
row-boundary latches for screen-start 1, row-table mode, and graphics mode,
and the scan-line/field boundary for display enable. Other initialization
fields currently decode on write; tests should not rely on deliberately late,
out-of-spec writes being delayed exactly like physical propagation.

## Character Generator, UDG RAM, and Dot Stretch

The AVDC produces character addresses and line addresses; the SCB2675 CMAC
serializes the glyph dots and applies attributes. The Partner board places a
fixed character ROM and a 2 KiB 6116 user character RAM between them.

Attribute-data bit 2 (`ATTD2`) selects character RAM. It provides 128 user
characters with 16 stored scan-line bytes each:

```text
UDG_address = 0x2000 + ((character & 0x7f) * 16) + line_0_to_15
```

The Partner RAM path swaps D0 and D7. The emulated nine-dot CMAC load order is
D7 through D0 followed by D8, with D8 tied/repeated from D7 on this path. This
wiring applies only to UDG RAM; do not swap the fixed character-ROM data.

See [`AVDC-UDG.md`](AVDC-UDG.md) for the complete programming sequence and a
runnable Z80 CP/M example.

Attribute-data bit 3 (`ATTD3`) drives CMAC `DOTS`; PIO B bit 0 does not. DOTS
is sampled at the falling edge of BLANK and controls the entire following scan
line. The frame-snapshot renderer therefore uses the first fetch address of a
scan line as the sampled attribute source. Treating it as a per-character
attribute produces impossible mid-line changes.

Dot stretch appends one extra on-dot after each individual dot or contiguous
run. It does not double every set pixel and it does not change CCLK or the
number of displayed character positions. It is distinct from:

- `C1:C0`, which sets the number of dot periods per character and CCLK;
- `DOTM/ADOTM`, which divides dot shifting for dot-width modulation;
- double width, which causes the CMAC to ignore the following character fetch.

## Regression Coverage

- `test_ef9367`: vector endpoints/directions, four line styles, phase-aware
  BUSY timing, long clear timing, XOR with pen and eraser, blocks, banks, and
  board inputs.
- `test_scn2674`: all IR decodes, screen starts, buffer wrap, automatic
  splits, scrolling, delayed commands, active-display blank waits, row-table
  bus slots, raster sync, CMAC divider ratios, interlace, and interface latches.
- `test_partner_gdp_memacc`: exact 80/132-column `idp-games` init strings,
  DADD13/LL restriction-latch timing, repeated READY inside one safe access
  span, PIO programming, 18/24 MHz and character-width controls, UDG wiring,
  scan-line-wide dot stretch, page selection, XOR, and scroll interaction.

When changing timing, test both raw chip-clock counts and their board-level
conversion. A visually plausible screenshot is not sufficient evidence that
READY/RDFLG, split, or blanking timing is correct.

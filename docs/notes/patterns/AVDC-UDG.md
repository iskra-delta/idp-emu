# Partner GDP User-Defined Characters

This note describes how a Z80 program defines and displays a user-defined
character (UDG) on the Partner GDP text display. It applies to the SCN2674
AVDC/SCB2675 character path, not to EF9367 graphics pixels.

The display must already be initialized and running. That is true after a
normal GDP CP/M boot. The example below is a complete CP/M `.COM` program: it
defines character code `21h`, writes it at the current AVDC cursor position,
and returns to CP/M.

## Memory layout

The Partner has a 2 KiB writable 6116 character RAM with 128 glyph slots and
16 bytes per glyph. Its AVDC address is:

```text
address = 2000h + ((character_code & 7fh) * 16) + scan_line
```

For code `21h`, the 16 bytes therefore occupy `2210h..221fh`. Define all 16
bytes even when the active display mode shows fewer than 16 scan lines.

The character byte in display memory selects the slot. Attribute bit 2
(`04h`) selects writable character RAM instead of the fixed character ROM:

```text
display character = 21h
display attribute = 04h
```

The RAM address `1000h` is not the UDG base. The Partner-tested `idp-games`
AVDC implementation uses `8 * 1024 + character * 16`, which is `2000h`.

## Partner wiring detail

The UDG data path swaps D0 and D7. The swap applies only to writable character
RAM, not to the fixed character ROM. If the bits of a source row are
`abcdefgh` from D7 to D0, the visible row is affected by exchanging the two
end bits. Either compensate when preparing the bytes or deliberately use rows
whose D7 and D0 values match, as the example does.

Attribute bit 3 (`08h`) is not another UDG-select bit. It drives the CMAC
`DOTS` input, which is sampled for a complete scan line and stretches dots
across that line.

## Ports and commands

| Port | Direction | Purpose |
| --- | --- | --- |
| `34h` | write | character/data latch |
| `35h` | write | attribute latch |
| `36h` bits 7/4 | read | active-low GDP pixel latch / Partner AVDC `RESTRICT` timing |
| `38h` | write | selected AVDC register data |
| `39h` bit 5 | read | AVDC ready (`RDFLG`) |
| `39h` | write | AVDC command |
| `3ch`, `3dh` | write | cursor address, low then high |

The commands used here are:

| Command | Meaning |
| --- | --- |
| `1ah` | select the display-pointer registers for two writes to port `38h` |
| `a2h` | write the character and attribute latches at the display pointer |
| `aah` | write the latches at the cursor without incrementing it |

Do not write AVDC display memory whenever it happens to be ready. First wait
for port `36h` bit 4 to become one and then zero. This returns at the start of
the board's safe access interval. Also wait for port `39h` bit 5 before issuing
the next memory operation.

Port `36h` is direction-sensitive on the Partner GDP board: reading gets the
GDP pixel latch on D7 and `RESTRICT` on D4, while writing controls the GDP
scroll latch. AVDC code must mask D4 as the example does.

## Complete Z80 CP/M example

This source uses the ASxxxx Z80 syntax shipped with SDCC. It needs no runtime
library and makes no BIOS-specific calls. CP/M starts it at `0100h` with a
valid stack. The final jump to address zero performs the normal CP/M warm
boot.

```asm
        .module udg_demo
        .area   UDGDEMO (ABS)

AVDC_CHR       .equ    0x34
AVDC_ATTR      .equ    0x35
AVDC_ACCESS    .equ    0x36
AVDC_INIT      .equ    0x38
AVDC_CMD       .equ    0x39

AVDC_READY     .equ    0x20
ACCESS_FLAG    .equ    0x10
SET_POINTER    .equ    0x1A
WRITE_POINTER  .equ    0xA2
WRITE_CURSOR   .equ    0xAA

UDG_SELECT     .equ    0x04
UDG_CODE       .equ    0x21
UDG_BASE       .equ    0x2000
UDG_ADDRESS    .equ    UDG_BASE + UDG_CODE * 16

        .org    0x0100

start:
        ld      hl,#UDG_ADDRESS
        ld      de,#glyph
        ld      b,#16

define_glyph:
        ld      a,(de)
        call    write_at_pointer
        inc     de
        inc     hl
        djnz    define_glyph

        ; Put code 21h with the UDG attribute at the current cursor.
        call    wait_access
        call    wait_ready
        ld      a,#UDG_CODE
        out     (AVDC_CHR),a
        ld      a,#UDG_SELECT
        out     (AVDC_ATTR),a
        ld      a,#WRITE_CURSOR
        out     (AVDC_CMD),a
        call    wait_ready

        jp      0x0000

; Write A to AVDC memory address HL without changing HL, DE, or BC.
write_at_pointer:
        push    bc
        ld      c,a
        call    wait_access
        call    wait_ready

        ld      a,#SET_POINTER
        out     (AVDC_CMD),a
        ld      a,l
        out     (AVDC_INIT),a
        ld      a,h
        and     #0x3F
        out     (AVDC_INIT),a

        ld      a,c
        out     (AVDC_CHR),a
        xor     a
        out     (AVDC_ATTR),a
        ld      a,#WRITE_POINTER
        out     (AVDC_CMD),a
        call    wait_ready
        pop     bc
        ret

; Wait for a complete restricted interval: high, then low.
wait_access:
wait_access_high:
        in      a,(AVDC_ACCESS)
        and     #ACCESS_FLAG
        jr      z,wait_access_high
wait_access_low:
        in      a,(AVDC_ACCESS)
        and     #ACCESS_FLAG
        jr      nz,wait_access_low
        ret

wait_ready:
        in      a,(AVDC_CMD)
        and     #AVDC_READY
        jr      z,wait_ready
        ret

; Eight visible rows followed by eight blank rows. D7 equals D0 in every
; nonblank byte, so the Partner's D0/D7 wiring swap leaves this face intact.
glyph:
        .db     0x3C,0x42,0xA5,0x81
        .db     0xA5,0x99,0x42,0x3C
        .db     0x00,0x00,0x00,0x00
        .db     0x00,0x00,0x00,0x00
```

Save it as `udg-demo.asm`, then build it with the SDCC tools:

```sh
sdasz80 -plosgff udg-demo.rel udg-demo.asm
sdldz80 -n -i udg-demo udg-demo.rel
makebin -p -s 65536 -o 256 udg-demo.ihx udg-demo.com
```

The resulting `udg-demo.com` is 111 bytes with the SDCC 4.2 ASxxxx tools. Put
it on a Partner CP/M disk using the usual transfer workflow, boot the GDP
machine, and run `UDG-DEMO`. A small face appears at the current text cursor.

The `wait_access` loop requires an active AVDC raster. Do not run this example
before the firmware has initialized and enabled the GDP text display.

## Common failures

- A normal ROM character appears: the display-cell attribute is missing bit 2.
- The wrong glyph or no glyph appears: the data was written at `1000h` or the
  character-code factor of 16 was omitted.
- The two edge pixels are reversed: the source rows did not account for the
  Partner D0/D7 swap.
- Only part of the character is stable after a mode change: fewer than all 16
  row bytes were initialized.
- The program hangs in `wait_access`: the AVDC display is not running.
- Intermittent display-memory corruption: the `RESTRICT` high-to-low wait or
  the AVDC ready check was skipped.

## References

- [Partner GDP schematic](<../../vendor/IDC-Partner GDP_nacrt.pdf>) — 6116
  character RAM and SCB2675 data-path wiring.
- [SCN2674/SCB2675 source index](../../vendor/README.md) — local controller
  and CMAC data sheets.
- [`idp-games` AVDC implementation](https://github.com/iskra-delta/idp-games/blob/master/src/common/avdc.c) — Partner-tested UDG base and write sequence.
- [`GDP-AVDC-CMAC-TIMING.md`](GDP-AVDC-CMAC-TIMING.md) — full display timing,
  access-window, and attribute behavior.

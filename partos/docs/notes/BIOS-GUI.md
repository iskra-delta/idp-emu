# Note: BIOS Startup Screen Mockup

Category: BIOS / User Interface
Date(s): 2024-03-19, 2026-06-11 (merged from `2024-03-19_bios-gui`)

Mockup of the BIOS startup/summary screen. Device names are bare (no
`/DEV/` prefix) to save ROM string space.

~~~
Wecome to Iskra Delta Partner Model WF BIOS

        S E R I A L   P O R T S

                 IFACE     ALIAS     SPEED  B  P  S
        TTYS0    STDIN     STDIN   300  8  N  1
        TTYS1    STDOUT    STDOUT 9600  8  N  1
        TTYS2    MOUSE     MOUSE 2400  8  N  1
        TTYS3    -

        P A R A L L E L   P O R T S

                IFACE     ALIAS
        LP0     printer   PRINTER
        LP1          -

        R E A L   T I M E   C L O C K

                DATE         TIME
        RTC     05/10/2023   18:06:22

        H A R D   D I S K S

             TYPE    SIZE   CYLS  HEAD  PRECOMP  LANDZ  SECTOR
        SDA  SEAGATE 10 MB  519   8
        SDB  WD      40 MB

        F L O P P Y   D R I V E S

            TYPE  SIZE    SIDE TRACK SEC/TRK SEC.SIZE
        FD0 3.5"  1.440kB    2
        FD1 5.25" 360kB      1

Press F1 for information about the BIOS.
~~~

## 2026-06-11 context

A screen like this will not fit the 2 KB ROM budget alongside drivers; it
belongs to the "BIOS loaded from disk" variant or to an OS-level utility.
Kept as the target look & feel and for the device-naming scheme
(TTYSn, FDn, SDn, RTC, LPn), which the driver format may want to adopt.

## 2026-06-13 setup/NVRAM direction

The BIOS setup screen should store only compact selectors in the MM58167
NVRAM, not full geometry values. Hard disk and floppy parameters are kept
in ROM tables and selected by small type indexes.

BIOS setup should be applied only after hardware detection:

1. Detect the currently present devices.
2. Validate the 8-byte NVRAM setup block checksum.
3. If the checksum is invalid, `RESET TO FACTORY SETTINGS`.
4. Open/setup using the detected hardware list plus the validated or reset
   configuration.

This packing uses all 8 NVRAM bytes. To keep four 1-byte serial config
entries, `byte 7` is used by `ttys3` config and is not currently reserved.

### Boot device selector

The boot target uses 4 bits:

- `0x0` = `sda`
- `0x1` = `sdb`
- `0x2` = `fd0`
- `0x3` = `fd1`
- `0x4` = `fd2`
- `0x5` = `fd3`
- `0x6`-`0xf` = reserved

### BIOS settings block

Bit numbering below treats bit 7 as the first/high bit of a byte and bit 0
as the last/low bit.

- `byte 0`
  `bits 7:4` setup checksum
  `bits 3:0` boot device selector
- `byte 1`
  `bits 7:6` `fd0` type index
  `bits 5:4` `fd1` type index
  `bits 3:2` `fd2` type index
  `bits 1:0` `fd3` type index
- `byte 2`
  `bits 7:6` `sda` type index
  `bits 5:4` `sdb` type index
  `bits 3:2` `lp0` attachment kind
  `bits 1:0` `lp1` attachment kind
- `byte 3`
  `bits 7:6` `ttys0` attached device kind
  `bits 5:4` `ttys1` attached device kind
  `bits 3:2` `ttys2` attached device kind
  `bits 1:0` `ttys3` attached device kind
- `byte 4`
  `ttys0` line format/config
  `bits 7:5` speed code
  `bit 4` stop bits
  `bits 3:2` parity
  `bit 1` data bits
  `bit 0` reserved
- `byte 5`
  `ttys1` line format/config
  `bits 7:5` speed code
  `bit 4` stop bits
  `bits 3:2` parity
  `bit 1` data bits
  `bit 0` reserved
- `byte 6`
  `ttys2` line format/config
  `bits 7:5` speed code
  `bit 4` stop bits
  `bits 3:2` parity
  `bit 1` data bits
  `bit 0` reserved
- `byte 7`
  `ttys3` line format/config
  `bits 7:5` speed code
  `bit 4` stop bits
  `bits 3:2` parity
  `bit 1` data bits
  `bit 0` reserved

### Parallel-port attachment selector

Each parallel port uses 2 bits:

- `0` = nothing
- `1` = printer
- `2` = covox
- `3` = free

### Serial-port config byte

Each serial port gets one full byte:

- `bits 7:5` speed code
- `bit 4` stop bits
  `0` = 1 stop bit
  `1` = 2 stop bits
- `bits 3:2` parity
  `0` = none
  `1` = odd
  `2` = even
  `3` = reserved
- `bit 1` data bits
  `0` = 7 bits
  `1` = 8 bits
- `bit 0` reserved

The 3-bit speed code indexes a ROM-defined baud table:

- `000` = `300`
- `001` = `600`
- `010` = `1200`
- `011` = `2400`
- `100` = `4800`
- `101` = `9600`
- `110` = `19200`
- `111` = free

This leaves one spare low bit in each serial config byte for future BIOS use.

### Serial-port attached device selector

Each serial port gets a 2-bit attachment selector packed into `byte 3`:

- `0` = `keyboard` (input only)
- `1` = `terminal`
- `2` = mouse
- `3` = free

Attachment packing is:

- `ttys0` = `byte 3`, `bits 7:6`
- `ttys1` = `byte 3`, `bits 5:4`
- `ttys2` = `byte 3`, `bits 3:2`
- `ttys3` = `byte 3`, `bits 1:0`

### Setup checksum

The high nibble of `byte 0` stores a 4-bit setup checksum:

- `byte 0`, `bits 7:4` = checksum nibble

Recommended encoding:

- sum the other 15 nibbles of the 8-byte setup block
- store `(-sum) & 0x0f` in `byte 0`, `bits 7:4`
- validation passes when the sum of all 16 nibbles is `0 mod 16`

If validation fails, BIOS must treat the setup block as invalid and
`RESET TO FACTORY SETTINGS`.

This means:

- hard disks currently get 2-bit type selectors, so up to 4 hard-coded ROM
  drive profiles are supported for `sda` and `sdb`
- floppy drives also get 2-bit selectors, so up to 4 hard-coded ROM floppy
  profiles are supported for `fd0`..`fd3`
- parallel ports get 2-bit attachment selectors for `lp0` and `lp1`
- serial ports get 2-bit attached-device selectors packed into `byte 3`
- serial ports get one full byte each for `ttys0`..`ttys3` in `bytes 4..7`
- the high nibble of `byte 0` is reserved for setup checksum validation
- the summary/setup screen should display the decoded ROM profile, not the
  raw NVRAM value

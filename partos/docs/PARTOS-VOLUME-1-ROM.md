# PARTOS VOLUME 1: ROM

I think of the PartOS ROM as the smallest honest piece of the system. It
does not try to be the whole operating system. It does just enough to make
the machine predictable: detect the hardware, recover sane settings, offer a
short setup window, load the operating-system image, and hand off cleanly.

## What This Volume Covers

This volume describes the code in `partos/src/rom/` and the boot-time
behavior that the emulator and the real machine are expected to see.

## The Hardware Reality the ROM Lives In

The Partner gives the Z80 a split address space:

| Range | Size | Meaning |
|---|---:|---|
| `0x0000..0xBFFF` | 48 KiB | banked RAM window |
| `0xC000..0xFFFF` | 16 KiB | common RAM, always visible |

At reset, a `2 KiB` ROM overlay sits on the low address space and is mirrored
at:

- `0x0000`
- `0x0800`
- `0x1000`
- `0x1800`

The dangerous quirk is that ports `0x80..0x97` react to touches, not only to
writes:

| Port touch | Effect |
|---|---|
| `0x80..0x87` | disable ROM overlay |
| `0x88..0x8F` | select logical bank 0 |
| `0x90..0x97` | select logical bank 1 |

That one quirk explains most of the ROM shape. The code can keep running from
ROM only while it stays below `0x2000` and while it does not need writable RAM
under the overlay. That is why the current stage 1 is expanded above the
overlay window and only then turns the ROM off.

## Why the ROM Is Two-Stage

The physical ROM is too small for the whole firmware in plain form, so the
current design is deliberately two-stage.

Current verified build snapshot:

| Item | Value |
|---|---:|
| physical ROM size | `2048` bytes |
| stage 0 bootstrap | `84` bytes |
| stage 1 linked size | `2490` bytes |
| stage 1 compressed size | `1892` bytes |
| free ROM space | `72` bytes |

In practical terms:

- stage 0 is the tiny fixed bootstrap at `0x0000`
- stage 1 is the rest of the ROM firmware, linked for RAM execution at
  `0x2000`
- stage 1 is stored compressed inside the ROM image
- stage 0 keeps the ZX0 decoder in ROM, expands stage 1 directly into safe
  RAM above the overlay window, and jumps there
- stage 1 itself disables the overlay as its first act

## Boot Flow

I find the current boot easiest to understand as five short steps.

### 1. Reset and stage 0

Execution starts in ROM at `0x0000`.

Stage 0:

1. disables interrupts
2. establishes a temporary stack
3. points `HL` at the compressed stage-1 payload in ROM
4. points `DE` at `0x2000`
5. calls the ROM-resident ZX0 decoder
6. jumps to `0x2000`

At `0x2000`, stage 1 disables the overlay with `out (0x80),a` and continues
in ordinary RAM from there.

### 2. Stage 1 early setup

`start.s` is the first decompressed code. It:

1. detects the current machine model
2. reads the 8-byte MM58167 NVRAM block
3. validates the 4-bit nibble checksum
4. rewrites factory defaults if the checksum is bad
5. brings up GDP text mode on GDP-capable machines
6. prints the `PARTOS` banner

The model byte is cached as:

- `0x00` = plain text Partner
- `0x01` = GDP-capable Partner

### 3. Setup window

After the banner, the ROM watches for raw byte `0xFE` for roughly three
seconds.

- On the real Partner, that is the `SETUP` key.
- In the emulator, it is usually mapped to `Delete`, `Pause`, or `F12`.

If `0xFE` arrives in time, the ROM jumps into the setup menu. Otherwise it
continues to boot.

### 4. Boot-device search

The current policy is intentionally simple:

1. print `BOOTING`
2. try `fd0`
3. if that fails, initialize the selected `sda` geometry and try the hard
   disk
4. if both fail, print `NO BOOT DEVICE` and halt

### 5. OS load and handoff

The ROM loads `33` raw `256`-byte sectors or blocks:

| Medium block | Destination |
|---|---|
| `0` | `0xDF00..0xDFFF` |
| `1..32` | `0xE000..0xFFFF` |

Then it checks the staged boot record:

- byte `254` must be `0x55`
- byte `255` must be `0xAA`

If the signature is valid, the ROM sets up the live handoff like this:

- `HL = 0xE000`
- `B = model byte`
- `A` also happens to contain the model byte on the current path, but that is
  not a stable promise
- `C` and `D` are not initialized by the ROM today

Then it jumps to `0xFF6B`, which is the fixed address of
`__sys_page0_install` inside the loaded image.

## Setup Screen and Saved Configuration

The ROM setup is compact on purpose. It stores tiny selectors, not verbose
records.

### Display paths

- Plain Partner: serial terminal path
- GDP Partner: AVDC text mode

Both paths present the same logical setup content.

### Current actions

- arrow keys move between fields and change values
- `Ctrl+S` saves and exits
- `Ctrl+C` exits without saving

### Current fields

Serial:

- `ttyS0`
- `ttyS1`
- `ttyS2`
- `ttyS3`

Choices:

- `KEYBOARD`
- `TERMINAL`
- `MOUSE`
- `FREE`

Parallel:

- `lp0`
- `lp1`

Choices:

- `FREE`
- `PRINTER`
- `COVOX`

Hard disk:

- `sda`
- `sdb`

Choices:

- `FREE`
- `ST-506`
- `ST-412`
- `ST-225`

Floppy:

- `fd0`
- `fd1`
- `fd2`
- `fd3`

Choices:

- `FREE`
- `PARTNER`
- `DOS-720K`
- `DOS-360K`

### Factory defaults

The writable setup bytes start with these defaults:

- `ttyS0 = KEYBOARD`
- `ttyS1 = TERMINAL`
- `ttyS2 = MOUSE`
- `ttyS3 = FREE`
- `lp0 = PRINTER`
- `lp1 = FREE`
- `sda = ST-412`
- `sdb = FREE`
- `fd0 = PARTNER`
- `fd1 = FREE`
- `fd2 = FREE`
- `fd3 = FREE`

### Exact NVRAM layout used now

The current block is 8 bytes wide. Only a few fields are live.

| Byte | Meaning |
|---|---|
| `0` | unused |
| `1` | `fd0[7:6] fd1[5:4] fd2[3:2] fd3[1:0]` |
| `2` | `sda[7:6] sdb[5:4]` |
| `3` | `ttyS0[7:6] ttyS1[5:4] ttyS2[3:2] ttyS3[1:0]` |
| `4` | `lp0[7:6] lp1[5:4]` |
| `5` | unused |
| `6` | unused |
| `7` | low nibble = checksum adjuster, high nibble unused |

Selector values:

- serial: `0=KEYBOARD`, `1=TERMINAL`, `2=MOUSE`, `3=FREE`
- parallel: `0=FREE`, `1=PRINTER`, `2=COVOX`
- hard disk: `0=FREE`, `1=ST-506`, `2=ST-412`, `3=ST-225`
- floppy: `0=FREE`, `1=PARTNER`, `2=DOS-720K`, `3=DOS-360K`

### Checksum rule

The ROM validates the setup block by:

1. summing all 16 nibbles of the 8-byte block
2. keeping only the low 4 bits
3. storing `(-sum) & 0x0F` in the low nibble of byte `7`

Validation succeeds when the total nibble sum is `0 mod 16`.

## Storage Layout and Boot Media

The ROM does not parse FAT. That is a very important limitation to keep in
mind. FAT compatibility exists in the disk layout and tooling, not in the
boot reader itself.

### Floppy path

The ROM floppy reader:

- initializes the i8272
- turns the motor path on
- recalibrates the drive
- reads raw `256`-byte sectors in MFM mode

Current ROM limits:

- boot only tries `fd0`
- only the first `33` sectors matter
- only cylinder 0 matters to the ROM boot path
- geometry is treated as `80 x 2 x 18 x 256`

### Hard-disk path

The ROM hard-disk reader:

- reads the saved `sda` selector from NVRAM
- sends Xebec drive characteristics for that type
- performs polled SASI `READ(6)` in `256`-byte blocks

Current built-in types:

- `ST-506`
- `ST-412`
- `ST-225`

### FAT-oriented media layout

`tools/mkdosdisk.py` builds Partner-friendly superfloppy images with:

- `256`-byte logical sectors
- no MBR partition table
- a BPB in sector `0`
- `33` reserved sectors total
- normal FAT region after that
- normal fixed FAT12 or FAT16 root directory after that

The reserved region does double duty:

- FAT sees it as the reserved area before the FATs
- the ROM sees it as "boot sector plus `8 KiB` of raw OS sectors"

Current helper geometries:

| Image | Geometry | Size |
|---|---|---:|
| floppy DOS image | `80 x 2 x 18 x 256` | `737280` bytes |
| hard-disk DOS image | `306 x 4 x 32 x 256` | `10027008` bytes |

## Emulator Support That Matters for the ROM

The current emulator already does the parts the ROM needs for bring-up:

- `--rom` loads the ROM image
- `--fd0` and `--fd1` mount floppy images
- `--hdd` mounts the SASI/Xebec hard-disk image
- `--nvram` selects the MM58167 shadow file
- `--terminal vt52|vt100` selects the terminal profile

### Terminal behavior

The shared terminal engine currently supports:

- VT52 cursor motion
- clear screen and clear line
- cursor show and hide
- ANSI SGR `0`, `1`, `7`, `22`, `27`

The CRT renderer currently uses:

- `80x24` text cells
- `11x16` character cells
- a 1-pixel gap between characters
- curved safety margins so text stays inside the bezel

Monitor presets currently include:

- `Flat (No Effects)`
- `Green CRT`
- `Orange CRT`
- `BW CRT`
- `LCD`

### Useful host key mapping

- arrow keys send VT52 cursor sequences
- `Ctrl+letter` sends the matching control code
- `Delete`, `Pause`, and `F12` send raw `0xFE`

### Test coverage that matters

The tree currently has automated checks for:

- ROM banners on CRT and GDP
- setup rendering on CRT and GDP
- NVRAM recovery
- terminal highlight, inverse, clear-screen, and cursor-hide behavior
- CRT terminal routing across SIO channels
- byte-for-byte ROM OS loading up to the `0xFF6B` handoff point

## Source Map

These are the main ROM-side files and what I use them for:

- `bootstrap.s` and `dzx0_standard.s`: fixed stage-0 loader and ZX0 decoder
- `start.s`: decompressed stage-1 entry and boot policy
- `bios.s`: ROM setup screen and saved selector handling
- `menu.s`: compact menu engine
- `nvram.s`: NVRAM read, write, default, and checksum helpers
- `kbd.s`: direct keyboard polling on SIO0A
- `print.s`: common ROM-side text output helpers
- `avdc.s`: GDP text-mode helpers
- `fd.s` and `fd.inc`: floppy boot reader
- `hd.s` and `hd.inc`: hard-disk type tables and boot reader
- `model.s`: machine-model detection

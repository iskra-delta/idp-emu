# PARTOS VOLUME 1: ROM

This volume describes the code in `partos/src/rom/` — the firmware that runs
from the instant you power the machine on until the operating system takes over.
It assumes you know the Z80 and basic disk I/O, but nothing about this machine.

## Current Milestone

At the end of the current boot milestone, the ROM side is considered
**implemented and verified** for the hard-disk boot path.

What works today:

- stage 0 decompresses stage 1 into RAM at `0x2000`
- stage 1 disables the ROM overlay immediately after entering RAM
- the **boot sector** (sector `0`) is read first to the scratch buffer at
  `0x1800` and checked as the boot gate before any OS sectors are loaded
- reserved sectors `1..8` are loaded to `0x0000`
- reserved sectors `9..72` are loaded to `0xC000`
- the ROM jumps to the micro-kernel entry at `0x0000`

What this has been verified against:

- `idp-bootload-probe` proves the split image is loaded to the exact RAM
  addresses the ROM contract requires
- `idp-full-boot-probe` proves the full chain continues past the ROM handoff,
  through the kernel, and reaches the OS bootstrap thread

What is still open on the ROM side:

- full-image **floppy** boot is not yet the verified target, because the ROM
  floppy reader still needs multi-cylinder reads for the now-larger reserved
  region; the hard-disk path is the working reference path

## What the ROM Is

When you power on or reset the Partner, the Z80 begins executing instructions at
memory address `0x0000`. Whatever sits at `0x0000` is therefore the first
program that runs. On the Partner that is a read-only memory chip — the ROM —
holding exactly **2048 bytes (2 KiB)**.

The ROM has to do a lot for 2 KiB: identify the machine, let you edit and
remember its settings, find a boot disk, load the operating system off it, and
start it. That work does not fit in 2 KiB as plain Z80 code. The ROM gets around
this by storing most of itself **compressed**, and unpacking itself into RAM
before it does any real work. Almost everything else about the ROM's shape
follows from that one decision.

Here is what is actually in the chip, from the current build:

| Item | Size |
|---|---:|
| physical ROM | `2048` bytes |
| stage 0 — bootstrap + decompressor (plain code) | `84` bytes |
| stage 1 — the real firmware, stored compressed | `1961` bytes |
| stage 1 once decompressed in RAM | `2568` bytes |
| unused ROM space | `3` bytes |

So the ROM is two programs: a tiny **stage 0** that runs in place, and a larger
**stage 1** that stage 0 unpacks into RAM and runs there. Notice stage 1 is
bigger than the whole chip — that is only possible because it is stored
compressed.

## The Memory the ROM Works In

Two facts about Partner memory that the rest of this depends on.

**Banked low, common high.** The bottom 48 KiB of the address space
(`0x0000`–`0xBFFF`) is *banked*: there are two separate 48 KiB banks of RAM, and
a hardware switch chooses which one is visible there at any moment. The top
16 KiB (`0xC000`–`0xFFFF`) is *common*: it is the same physical RAM no matter
which bank is selected.

| Range | Size | Meaning |
|---|---:|---|
| `0x0000..0xBFFF` | 48 KiB | banked — one of two RAM banks is visible here |
| `0xC000..0xFFFF` | 16 KiB | common — always the same RAM |

**The ROM overlay.** At power-on the ROM is mapped at the bottom of the address
space — that is where the Z80 starts fetching, and it hides the RAM underneath.
Before stage 1 can use that low RAM, the firmware switches the overlay off by
writing an I/O port. These are the ports the firmware actually uses:

| Port written | Firmware's intent |
|---|---|
| `0x80` | disable the ROM overlay (reveal the RAM underneath) |
| `0x88` | select bank 0 in the low window |
| `0x90` | select bank 1 in the low window |

Stage 1 is unpacked to, and run from, `0x2000`, and one of its first actions is
to write `0x80` to turn the overlay off; from that point the low address space
is plain RAM.

## The Boot, Step by Step

From power-on to the operating system starting, the ROM runs the sequence
below. It is a straight line — nothing here loops back.

### 1. Power-on: stage 0 runs from ROM

The Z80 starts at `0x0000`, executing stage 0 directly out of the ROM chip.
Stage 0 is 84 bytes and does only what it must while still running from ROM:

1. disable interrupts — nothing is set up to handle them yet
2. set the stack pointer to `0xBFFF`, which is real RAM, not under the overlay
3. start unpacking stage 1

### 2. Unpack stage 1 into RAM — the decompression step

Stage 1 sits in the chip immediately after stage 0, stored compressed (ZX0
format). Stage 0 contains a small ZX0 decompressor. It reads the compressed
bytes from ROM and writes the expanded program to RAM beginning at `0x2000`:

- source: the compressed payload in ROM
- destination: `0x2000`
- result: `1961` compressed bytes expand into `2568` bytes of runnable code

When the expansion finishes, stage 0 jumps to `0x2000`. This is the moment the
firmware stops being a ROM program and becomes a RAM program: from here on it
executes from the copy in RAM, not from the chip.

### 3. Turn the ROM overlay off

The first thing stage 1 does at `0x2000` is touch port `0x80`, disabling the ROM
overlay. The low memory the ROM had been hiding is now plain RAM. The chip's job
as a memory device is over; everything below this point runs entirely from RAM.

### 4. Identify the machine and recover its settings

Now stage 1 brings the machine to a known state:

1. **Detect the model** — plain-text Partner or graphics-capable (GDP) — and
   remember it (`0x00` = text, `0x01` = GDP). Later steps use this to pick the
   right display path.
2. **Read the saved settings.** The Partner keeps an 8-byte settings block in
   the battery-backed RAM of its real-time-clock chip (the NVRAM). Stage 1 reads
   it.
3. **Check and repair it.** It verifies a 4-bit checksum over the block. If the
   checksum is wrong — corrupted, or never initialised — it writes the factory
   defaults back into the NVRAM, so every later read can trust the block.
4. **Show signs of life.** On GDP machines it brings up text mode, then prints
   the `PARTOS` banner.

### 5. The setup window

For about three seconds after the banner, stage 1 watches the keyboard for one
key — the `SETUP` key, which arrives as raw byte `0xFE`.

- **If `SETUP` is pressed**, the ROM enters its setup menu. There you choose what
  is attached to each port — the four serial channels, the parallel ports, the
  hard-disk type, the floppy type. When you save, those choices are packed back
  into the 8-byte NVRAM block with a fresh checksum; if you exit without saving,
  the block is left untouched. Either way the ROM then continues to boot. (The
  exact fields, their encoding, and the checksum rule are documented later in
  this volume.)
- **If nothing is pressed in time**, the ROM goes straight to boot.

### 6. Find a boot disk

The ROM prints `BOOTING` and looks for something to boot from, in a fixed order:

1. the floppy `fd0`
2. failing that, the hard disk `sda`
3. failing both, it prints `NO BOOT DEVICE` and halts

For whichever device it is trying, it reads **sector 0** (the boot record) into a
small scratch buffer at `0x1800`, and checks a single signature byte (offset
`254` must be `0x55`). If that byte is wrong the disk is not bootable and the ROM
moves on to the next device. This is only a quick gate — the operating system
re-reads and fully checksums the boot record once it is running.

> The ROM does **not** understand FAT, directories, or files. The operating
> system it loads is not stored as a file. It lives in the disk's *reserved
> sectors* — the raw sectors between the boot record and the start of the
> filesystem — and the ROM reads them directly by sector number.

### 7. Copy the operating system into RAM

The OS image is **18 KiB**, held in **72 reserved sectors** of `256` bytes each
(sectors `1` through `72`, right after the boot record). It is built in two parts
that belong at two different addresses, so the ROM loads it as two regions:

| Reserved sectors | Bytes | Loaded to | What it is |
|---|---:|---|---|
| `1..8` | `2 KiB` | `0x0000` | the micro-kernel — the small, always-resident core |
| `9..72` | `16 KiB` | `0xC000` | the rest of the OS — services, drivers, and their data |

The copy is one simple loop: read a 256-byte sector, write it to the
destination, advance the destination by 256, repeat. It starts writing at
`0x0000`; once the first 8 sectors (the 2 KiB micro-kernel) are in place, it
switches the destination to `0xC000` and continues for the remaining 64 sectors.

Both destinations are safe to overwrite:

- `0x0000`–`0x07FF` held stage 0, which has already finished and jumped to
  `0x2000`, so it is free memory now.
- `0xC000`–`0xFFFF` is above the boot stack at `0xBFFF`, so writing the 16 KiB
  region never disturbs the stack the loader is still using.

### 8. Hand off to the kernel

Finally the ROM puts the model byte in register `B` and jumps to `0x0000`, the
start of the loaded micro-kernel. That jump ends the ROM's job.

What it hands over is deliberately minimal:

- the micro-kernel is in RAM at `0x0000`, but only in the **currently selected
  bank**
- the rest of the OS is at `0xC000`, in common RAM that every bank sees
- `B` holds the model byte

Everything after this — mirroring the low page into the second bank,
initialising the kernel, and starting the first OS payload thread at `0xC000` —
is the kernel's work, and is covered in Volume 2.

For the current milestone, that next handoff has been exercised end-to-end:
the verified ROM path does not stop at the jump itself, but continues through
kernel entry and reaches the OS bootstrap handoff.

### A note on the fixed load addresses

The two load addresses, `0x0000` and `0xC000`, are fixed constants chosen on
purpose. An earlier version of the ROM loaded the OS as a single block to an
address *computed from how the kernel happened to link* — so every code change
shifted it, and the ROM, the disk-image tool, and the test harnesses all had to
chase the moving number. Pinning the two parts to `0x0000` and `0xC000` means
they always link at the same place, and the ROM's load targets never change
again.

## Setup and Saved Configuration

The ROM has a small setup screen (code in `bios.s` and `menu.s`), reached with the
SETUP key during the boot window (step 5). It lets you declare what is attached to
each port. On a plain Partner it draws over the serial terminal; on a GDP machine
it uses AVDC text mode. Arrow keys move and change a field, `Ctrl+S` saves and
exits, `Ctrl+C` exits without saving.

### How it is stored

Everything the setup screen edits lives in **8 bytes** of the MM58167 clock's
battery-backed RAM (the NVRAM). Each selector is 2 bits, so one byte holds up to
four of them. The whole block — which byte and bits hold each field, what each
2-bit value means, and the factory default — is one table:

| Field | Byte | Bits | `0` | `1` | `2` | `3` | Default |
|---|---:|---|---|---|---|---|---|
| `fd0` | 1 | 7:6 | FREE | PARTNER | DOS-720K | DOS-360K | PARTNER |
| `fd1` | 1 | 5:4 | FREE | PARTNER | DOS-720K | DOS-360K | FREE |
| `fd2` | 1 | 3:2 | FREE | PARTNER | DOS-720K | DOS-360K | FREE |
| `fd3` | 1 | 1:0 | FREE | PARTNER | DOS-720K | DOS-360K | FREE |
| `sda` | 2 | 7:6 | FREE | ST-506 | ST-412 | ST-225 | ST-412 |
| `sdb` | 2 | 5:4 | FREE | ST-506 | ST-412 | ST-225 | FREE |
| `ttyS0` | 3 | 7:6 | KEYBOARD | TERMINAL | MOUSE | FREE | KEYBOARD |
| `ttyS1` | 3 | 5:4 | KEYBOARD | TERMINAL | MOUSE | FREE | TERMINAL |
| `ttyS2` | 3 | 3:2 | KEYBOARD | TERMINAL | MOUSE | FREE | MOUSE |
| `ttyS3` | 3 | 1:0 | KEYBOARD | TERMINAL | MOUSE | FREE | FREE |
| `lp0` | 4 | 7:6 | FREE | PRINTER | COVOX | — | PRINTER |
| `lp1` | 4 | 5:4 | FREE | PRINTER | COVOX | — | FREE |

Bytes `0`, `5`, and `6` are unused. Byte `7` carries the checksum: its low nibble
is chosen so the sum of all 16 nibbles of the block is `0 mod 16`. The ROM checks
the block by summing those nibbles; if the low 4 bits are not zero it treats the
block as corrupt and writes the factory defaults back (this is the repair step in
boot step 4).

## How the ROM Reads Disks

The ROM does not understand FAT. It reads the boot record and the reserved
sectors as raw, numbered sectors; making sense of the filesystem is the OS's job.
It has two readers, and both take their geometry from the setup selectors — the
ROM does not assume a fixed disk format.

### Floppy

The floppy reader brings up the i8272, starts the motor, recalibrates to track 0,
and transfers sectors with polled I/O. Its geometry is **not** hardcoded: at boot
it reads the `fd0` type from NVRAM (byte 1, bits 7:6) and looks the geometry up in
a small table:

| `fd0` type | Sectors/track | Sector size |
|---|---:|---:|
| PARTNER | 18 | 256 B |
| DOS-720K | 9 | 512 B |
| DOS-360K | 9 | 512 B |

So a 720K or 360K disk *is* read with the correct `9`-sector / `512`-byte
geometry — provided `fd0` is set to that type in setup. An unset drive defaults to
PARTNER. The reader uses sectors/track to turn a logical block into a head and
sector, and feeds the sector-size code straight into the FDC read command.

The real limit today is elsewhere: the reader does **not seek** — it only
addresses cylinder 0. That was fine when the OS staging area fit in one cylinder,
but the `73`-sector reserved region no longer does (PARTNER offers only
`18 x 2 = 36` sectors in cylinder 0), so booting the full image off floppy needs
multi-cylinder reads. That work is still open; the hard-disk path has no such
limit and is the verified boot target today.

### Hard disk

The hard-disk reader reads the `sda` type from NVRAM (byte 2, bits 7:6), sends the
matching Xebec drive characteristics, and performs polled SASI `READ(6)` in
`256`-byte blocks. Built-in types: ST-506, ST-412, ST-225. As with the floppy,
the geometry follows the selected type, not a fixed assumption.

## Source Map

The main ROM-side source files and what each one does:

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

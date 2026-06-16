# CHANGELOG

I used to keep a growing pile of working notes under `partos/docs/notes/`.
That was useful during bring-up, but it stopped being a friendly shape for
the project. This file replaces that note stack with one chronological story:
what changed, why it mattered, and what is still unsettled.

## 2026-06-16

- I collapsed the old split documentation into a simpler set:
  - `CHANGELOG.md`
  - `PARTOS-VOLUME-1-ROM.md`
  - `PARTOS-VOLUME-2-KERNEL.md`
  - `PARTOS-VOLUME-3-OS.md`
- I renamed `src/bios/` to `src/rom/` so the tree says what the code really
  is: ROM firmware, not a PC-style BIOS clone.
- I rechecked the live build products and wrote the numbers down exactly:
  - `partos.rom` = `2048` bytes
  - stage 0 bootstrap = `84` bytes
  - stage 1 linked size = `2490` bytes
  - stage 1 compressed size = `1892` bytes
  - free ROM space = `72` bytes
  - `kernel.bin` = `6120` bytes
- I simplified the stage-0 ROM path:
  - stage 1 now links at `0x2000`
  - the ZX0 decoder stays in ROM
  - stage 0 no longer copies a trampoline, decoder, or compressed payload to
    scratch RAM first
  - stage 1 disables the overlay itself as its first act
- I updated the emulator-facing documentation to match the current CRT
  terminal behavior:
  - `80x24` text
  - `11x16` cells
  - 1-pixel inter-character gap
  - curved safety margins
  - monitor presets including `BW CRT`
  - VT52 plus the small ANSI subset used by the ROM
- I also corrected the written ROM-to-kernel story so it matches the code as
  it exists today, not the older plan.

## 2026-06-15

- `tools/mkdosdisk.py` became the house tool for preparing Partner-friendly
  FAT images with `256`-byte sectors.
- The boot-media layout settled into a practical shape:
  - sector `0` is the boot sector plus BPB plus `0x55AA`
  - sectors `1..32` are the reserved `8 KiB` OS staging area
  - the rest of the image is normal FAT plus root directory plus data
- The emulator gained writable mounted images for both floppy and SASI/Xebec
  media, which makes OS bring-up much more realistic.

## 2026-06-14

- The ROM became a real two-stage design instead of a sketch:
  - stage 0 lives at `0x0000`
  - stage 1 is linked for `0x0800`
  - stage 1 is stored compressed in ROM and decompressed into RAM
- The ROM became model-independent in practice. One source tree now boots:
  - the plain text Partner
  - the GDP-capable Partner
- Setup storage was intentionally narrowed to compact selector values in the
  MM58167 NVRAM block instead of storing large descriptive records.
- Invalid setup NVRAM stopped being a passive warning. The ROM now repairs it
  back to factory defaults during boot so later code can trust the block.
- Boot policy was fixed to `fd0` first and `sda` second.
- The ROM loader contract settled into:
  - sector `0` to `0xDF00..0xDFFF`
  - sectors `1..32` to `0xE000..0xFFFF`
  - `0x55AA` required at bytes `254..255` of sector `0`

## 2026-06-11

- The PartOS tree was split into the current project shape:
  - `src/rom/`
  - `src/kernel/`
  - `src/drivers/`
  - `src/os/`
  - `docs/`
  - `build/`
  - `bin/`
- The kernel-side device model settled on:
  - one intrusive global device list
  - driver-owned static device instances
  - a `30`-byte `dev_s`
  - a `14`-byte `dev_drv_s`
- The generic list rule was fixed early and still matters:
  `next` is the first field of every listable structure.

## Still Open

These are the questions that are still live enough to deserve a permanent
spot in the log.

- The final on-disk OS image contract is not finished. The ROM knows how to
  load `33` raw sectors, but the exact structure of the `0xE000..0xFFFF`
  image is still more implied than formally specified.
- The page-0 installer accepts a richer `A/B/C/D/HL` metadata contract than
  the current ROM caller really populates. The long-term ABI still needs to
  be written down and then obeyed consistently.
- `sdb` already exists in ROM setup, but the kernel-side hard-disk probe
  still only publishes `sda`.
- The ROM boot order is fixed today. Whether that should remain policy or
  become a saved setup choice is still open.
- `src/os/` is still a placeholder for higher-level operating-system code.
  The boundary between "kernel services" and "OS programs" is conceptually
  clear, but the concrete software in that layer does not exist yet.

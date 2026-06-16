# PARTOS VOLUME 3: OS

This volume is the honest one. If Volume 1 is about the ROM that already
boots and Volume 2 is about the kernel that already lands safely in memory,
Volume 3 is about the part that is still mostly ahead of us.

There is a real direction here, but not a finished operating system yet.

## Current State

Right now the live PartOS build produces two concrete binary artifacts:

- `partos/bin/partos.rom`
- `partos/bin/kernel.bin`

`src/os/` exists as the reserved home for higher-level operating-system code,
but it does not yet contain the shell, utilities, or system programs that
would make the project feel like a complete OS.

So the current stack is:

- ROM: real
- kernel scaffold: real
- drivers: real
- higher-level OS layer: planned, not implemented

## What the ROM Expects from "the OS"

The ROM only knows a few simple facts:

1. it may enter setup for a few seconds after the `PARTOS` banner
2. it should try `fd0` first and `sda` second
3. it should read sector `0` plus sectors `1..32`
4. it should require `0x55AA` at the end of sector `0`
5. it should jump to `__sys_page0_install` at `0xFF6B`

That is enough to boot something, but it is not yet a full formal OS-image
specification.

The biggest gap is the continuation target:

- the ROM sets `HL = 0xE000`
- `__sys_page0_install` eventually jumps to `HL`
- the linked kernel proper begins at `0xE818`

So there is still a missing piece of written contract around what must live
at `0xE000..0xE817` in the reserved-sector image.

## Disk Format Foundation

Even though the ROM does raw sector reads, the media layout is already being
shaped to coexist with ordinary FAT expectations.

`tools/mkdosdisk.py` builds Partner-friendly superfloppy images with:

- `256`-byte sectors
- no MBR
- a BPB in sector `0`
- `33` reserved sectors total
- normal FAT region after the reserved area
- normal fixed root directory after the FATs

That reserved area is the bridge between today's ROM and tomorrow's OS:

- sector `0` is the boot record plus BPB plus `0x55AA`
- sectors `1..32` are an `8 KiB` staging window for OS code

Current geometries:

| Medium | Geometry | Size |
|---|---|---:|
| floppy | `80 x 2 x 18 x 256` | `737280` bytes |
| hard disk | `306 x 4 x 32 x 256` | `10027008` bytes |

The project goal is DOS-style and Atari-ST-friendly media layout. The ROM is
not the piece that provides that compatibility. The image format and future
OS code are.

## What Belongs in the OS Layer

I want the split between kernel and OS to stay conceptually clean.

The kernel and drivers should own:

- banking-sensitive low-level setup
- page 0 and vector policy
- interrupt plumbing
- heap and object primitives
- device enumeration and device I/O entry points

The higher-level OS should eventually own:

- boot-time system initialization beyond the current `halt` loop
- filesystem policy on top of block devices
- console and program-launch behavior
- shell and utilities
- long-lived system services
- user-facing error handling and configuration tools

`src/os/` is where that software should live once it exists.

## The Open Questions That Matter Most

These are not bookkeeping details. They are the decisions that will define
what PartOS feels like when it grows past bring-up.

### Final reserved-sector image format

The ROM loader behavior is stable enough to test, but the exact contents of
the `0xE000..0xFFFF` image still need a proper contract.

At minimum that contract should say:

- what code begins at `0xE000`
- where the linked kernel payload sits inside the image
- what metadata, if any, sits ahead of it
- how versioning will be handled

### Stable ROM-to-kernel ABI

`__sys_page0_install` can accept version, model, flags, meta1, and a
continuation address, but the current ROM caller only meaningfully supplies
the model and the continuation. The interface needs to be narrowed or made
real.

### Boot policy versus setup policy

Today the ROM always tries `fd0` and then `sda`. That is easy to reason
about, but it may or may not be the long-term user experience we want.

### Multi-disk growth

The ROM setup screen already has selectors for `sda` and `sdb`, but the
kernel currently only publishes `sda`. If the OS is going to treat setup as
truth, the driver layer will eventually need to catch up.

### User-space and service ABI

The kernel already exposes useful low-level pieces, but there is no stable
user-facing ABI above them yet. The OS volume is where that future contract
will need to be written.

## What I Would Call Success for This Volume

I do not need this volume to pretend the operating system is further along
than it is. I need it to do two simpler jobs well:

1. describe the current state without romance
2. leave a clear runway for the next stage of work

That is where PartOS stands today. The ROM can load. The kernel can land.
The operating system itself is the next chapter.

# PARTOS VOLUME 2: KERNEL

This volume is about the code that lives above the ROM and below any future
user-facing operating system. Right now that means two closely related parts:
the early kernel in `src/kernel/` and the hardware-facing driver layer in
`src/drivers/`.

The most important thing to say up front is also the simplest: the kernel is
real, but it is still early. It already has a genuine memory map, page-0
installer, vector table, heap, list helpers, system-object helpers, and
drivers. What it does not have yet is the rest of the operating-system life
around those pieces.

## Current Build Shape

The verified current kernel image is `partos/bin/kernel.bin`, and it is
`6120` bytes long.

Its linked top-of-memory layout is:

| Region | Address | Size | Meaning |
|---|---|---:|---|
| `_CODE` | `0xE818` | `4072` bytes | kernel code plus linked drivers |
| `_HEAP` | `0xF800` | `1536` bytes | early heap |
| `_IM2` | `0xFE00` | `256` bytes | reserved IM 2 page |
| `_PAGE0` | `0xFF00` | `256` bytes | page-0 image plus installer helpers |

Two small data areas also exist inside the final linked image:

| Region | Address | Size |
|---|---|---:|
| `_SYSVARS` | `0xFF00` | `4` bytes |
| `_INITIALIZED` | `0xFF04` | `20` bytes |

The practical result is that the kernel owns the very top of common RAM.

## The Real ROM-to-Kernel Boundary Today

This part deserves extra care because older comments in the tree described a
slightly grander contract than the live ROM caller actually uses.

### What the ROM really does now

After loading sector `0` to `0xDF00` and sectors `1..32` to `0xE000..0xFFFF`,
the ROM:

- sets `HL = 0xE000`
- sets `B = model byte`
- leaves `A` holding the model byte as a side effect of the current path
- does not intentionally initialize `C` or `D`
- jumps to `0xFF6B`

`0xFF6B` is the fixed address of `__sys_page0_install` inside the loaded
image.

### What `__sys_page0_install` supports

The installer itself still accepts the larger register contract:

- `A` = version byte
- `B` = model byte
- `C` = flags byte
- `D` = meta1 byte
- `HL` = continuation address

That is the capability of the routine. It is not the full reality of the
current ROM caller.

### What happens next

`__sys_page0_install`:

1. stores `HL` into a 2-byte scratch slot
2. writes version, model, flags, and meta bytes into the page-0 template
3. copies 8 bytes from MM58167 NVRAM ports `0xA8..0xAF` into the low-page
   info block
4. clears and installs page 0 into logical bank 0
5. clears and installs page 0 into logical bank 1
6. patches bit 0 of `__sys_flags` so each page-0 copy knows which bank it is
7. switches back to logical bank 0
8. jumps to the saved continuation address without using the stack

Because the live ROM caller sets `HL = 0xE000`, the continuation currently
goes to `0xE000`, not directly to `__sys_kernel`.

That detail is important. It means the reserved-sector image is expected to
contain valid continuation code at `0xE000`, while the linked kernel proper
still begins at `0xE818`. The final image contract is therefore not just
"drop `kernel.bin` into sectors 1..32 and forget about it."

## Page 0, Vectors, and Low-Memory Policy

The final page at `0xFF00..0xFFFF` contains both the copied low-page image
and the helper code used to install it.

### Copied low-page image

The copied image provides:

- `RST 0x00` with `di` and `jp __sys_entry`
- metadata bytes at offsets `0x04..0x07`:
  - `__sys_version`
  - `__sys_model`
  - `__sys_flags`
  - `__sys_meta1`
- `RST 0x08 .. 0x38` stubs that load handler addresses from the shared vector
  table and jump through them
- a 38-byte low-page info block
- an 8-byte NVRAM cache at `__sys_nvram_cache`
- NMI entry at `0x66`

### Shared vector table

The shared vector table lives in `_INITIALIZED` and currently defaults to:

- `__sys_vec_entry -> __sys_kernel`
- `__sys_vec_rst08 .. __sys_vec_rst30 -> ret`
- `__sys_vec_rst38 -> reti`
- `__sys_vec_nmi -> retn`

Only the reset entry currently points into live kernel behavior. The rest are
safe stubs until real services arrive.

## What the Kernel Actually Does Today

The current first shared-memory entry is `__sys_kernel` in `init.s`.

Today it:

1. executes `di`
2. sets `sp = 0xFFFF`
3. calls `_ir_init`
4. calls `_ir_disable`
5. initializes the heap with `mem_init(__sys_heap, 0x0600)`
6. falls into a `halt` loop

So the system already has a genuine landing zone after ROM handoff, but it
is still a scaffold rather than a finished early boot sequence.

## Core Kernel Services Already Present

The tree already contains some small but solid building blocks.

### List helpers

`list.s` provides intrusive single-linked-list helpers:

- `list_match_eq`
- `list_find`
- `list_iterate`
- `list_append`
- `list_insert`
- `list_remove`
- `list_remove_first`

Both native assembly entry points and SDCC `sdcccall(1)` wrappers are
exported.

### Heap and system objects

`mem.s` provides:

- `mem_init`
- `mem_allocate`
- `mem_free`
- `mem_free_owner`

`sysobj.s` provides:

- `so_create`
- `so_destroy`

Again, both native assembly and SDCC-callable wrappers exist.

### Interrupt reference counting

`ir.s` provides the small interrupt reference-counting layer used by the
current kernel entry.

## Driver Model

The current driver ABI is intentionally compact and static.

### `dev_drv_s`

`dev_drv_s` is `14` bytes:

| Offset | Size | Field |
|---|---:|---|
| `0` | 2 | `next` |
| `2` | 2 | `probe` |
| `4` | 2 | `open` |
| `6` | 2 | `close` |
| `8` | 2 | `read` |
| `10` | 2 | `write` |
| `12` | 2 | `ioctl` |

### `dev_s`

`dev_s` is `30` bytes:

| Offset | Size | Field |
|---|---:|---|
| `0` | 2 | `next` |
| `2` | 8 | `name[8]` |
| `10` | 1 | `reserved` |
| `11` | 1 | `flags` |
| `12` | 16 | `data[16]` |
| `28` | 2 | `driver` |

The design rule I want to preserve is that `next` is first in every listable
structure.

### Probe model

Probe is chain-based:

- a driver returns `HL = head_of_chain`
- `HL = 0` means "nothing found"
- `_dev_probe_all()` appends the chain to the single global list

Current probe order:

1. `sio_probe`
2. `rtc_probe`
3. `nvram_probe`
4. `gdp_probe`
5. `fd_probe`
6. `hd_probe`

### Return convention

The current assembly drivers use a blunt success convention:

- success: `HL = 0x0000`
- failure: `HL = 0xFFFF`

There is no rich errno-style scheme yet.

## Current Drivers

### `sio`

- publishes `ttyS0` through `ttyS3`
- `open()` programs the channel
- `read`, `write`, and `ioctl` are still stubbed

Per-device `data[]`:

- `0` = data port
- `1` = control port
- `2` = chip index
- `3` = channel index

### `rtc`

- publishes one `rtc` device
- transfers exactly 6 bytes:
  `sec, min, hour, mday, mon, year`
- converts between chip BCD and in-memory binary

### `nvram`

- publishes one `nvram` device
- reads or writes exactly 8 raw bytes from `0xA8..0xAF`

### `gdp`

- publishes one `gdp` device when GDP hardware responds
- `open()` initializes AVDC text mode
- `write()` sends text through the GDP path
- `ioctl()` supports:
  - cursor position
  - current text attribute
  - cursor visibility

Current GDP ioctls:

- `0x20` = `GDP_IOCTL_GETPOS`
- `0x21` = `GDP_IOCTL_SETPOS`
- `0x22` = `GDP_IOCTL_GETATTR`
- `0x23` = `GDP_IOCTL_SETATTR`
- `0x24` = `GDP_IOCTL_CURSOR_OFF`
- `0x25` = `GDP_IOCTL_CURSOR_ON`

### `fd`

- publishes `fd0`, `fd1`, and so on for detected floppy units
- `open()` resets the byte cursor and recalibrates the drive
- `read()` and `write()` require 256-byte alignment
- `ioctl()` uses the shared 24-bit byte-position helper

### `hd`

- publishes `sda` when the SASI/Xebec adapter responds
- `open()` resets the byte cursor
- `read()` and `write()` use SASI `READ(6)` and `WRITE(6)` in 256-byte blocks
- `ioctl()` uses the shared 24-bit byte-position helper

The ROM setup screen already knows about both `sda` and `sdb`, but the
kernel-side probe still only publishes `sda`.

## Shared Driver Helpers

`drv.s` provides the common helper layer used by the current block drivers.

Shared meaning inside `dev.data[]` for disk-style devices:

| Byte | Meaning |
|---|---|
| `0` | unit or target |
| `1` | byte position low |
| `2` | byte position mid |
| `3` | byte position high |

Shared helpers:

- `drv_reset_dev`
- `drv_open_pos0`
- `drv_prep_rw256`
- `drv_advance_pos256`
- `drv_ioctl_pos24`

## Hardware Rules the Kernel Must Respect

The same hardware facts that shape the ROM continue to matter here.

### Banking rules

- never probe `0x80..0x97` casually
- switching banks is done by touching those ports
- code running in banked RAM must never switch away from itself

### Important device ports

| Ports | Meaning |
|---|---|
| `0x10..0x12` | SASI / Xebec adapter |
| `0x20..0x2F` | EF9367 GDP |
| `0x34..0x3F` | SCN2674 AVDC |
| `0x98` | floppy motor latch / status |
| `0xA8..0xAF` | MM58167 NVRAM bytes |
| `0xD8..0xDB` | SIO chip 0 |
| `0xE0..0xE3` | SIO chip 1 |
| `0xF0..0xF1` | i8272 FDC |

### Absence policy

The current code assumes absent GDP or SASI/Xebec hardware usually reads as
`0xFF`, and the probe paths explicitly use that fact.

### Interrupt topology

The daisy-chain priority order is:

1. DMA
2. CTC
3. SIO chip 0
4. SIO chip 1
5. PIO

The floppy controller is separate and uses the external vector latch at
`0xE8`.

## Assembly Style I Want to Preserve

The tree already has a recognizable assembly style, and I would rather keep
it than let every file drift.

- labels start in column 0
- instructions, directives, and standalone comments start at column 12
- mnemonics and registers are lowercase
- area names are uppercase
- public symbols use `::`
- local labels end in `$`
- SDAS immediates use `#`
- routine headers document inputs, outputs, and destroyed registers

That style is not cosmetic fluff. On a codebase like this, consistent layout
is one of the cheapest ways to keep low-level work readable.

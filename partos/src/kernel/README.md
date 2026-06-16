# kernel

This directory holds the early PartOS kernel image that is linked into the
top of common RAM.

It is currently separate from the ROM BIOS build:

- `src/bios/` contains the ROM image code, including the in-ROM boot logic.
- `src/kernel/` contains the microkernel image that the ROM bootloader will
  load and then enter through the page-0 installer.
- `src/drivers/` contains device-support code.
- `os/` is reserved for higher-level operating-system software such as the
  shell and other programs.

Design notes for the kernel still live in:

- `partos/docs/notes/UKERNEL.md`

## Current bootstrap contract

The intended ROM-to-kernel handoff is:

1. ROM loads the linked kernel image into its final address in common RAM.
2. ROM sets up the bootstrap metadata registers for `__sys_page0_install`:
   - `A` = version byte
   - `B` = model byte
   - `C` = flags byte
   - `D` = meta1 byte
   - `HL` = continuation address, normally `__sys_kernel`
3. ROM jumps to `__sys_page0_install`.
4. `__sys_page0_install`:
   - reads 8 bytes of RTC NVRAM into `__sys_nvram_cache`
   - clears page 0 in logical bank 0
   - copies the page-0 template into logical bank 0
   - clears page 0 in logical bank 1
   - copies the page-0 template into logical bank 1
   - patches `__sys_flags` so bit 0 reflects the installed bank
   - returns to logical bank 0
   - jumps to the continuation address from `HL`
5. `__sys_kernel` begins execution in shared memory.
6. `__sys_kernel` currently:
   - disables interrupts
   - sets `sp` to `0xffff`
   - initializes the system heap with `mem_init(__sys_heap, 0x0600)`
   - parks in a `halt` loop

`__sys_page0_install` does not use the stack. It stores the continuation
address in a 2-byte scratch slot (`page0_ret$`) inside the final page.

## Current linked memory map

The kernel is linked so that the top of memory is reserved like this:

```text
0xe816..0xf7e7  _CODE          kernel code + linked drivers
0xf7e8..0xf7eb  _SYSVARS       kernel irq state
0xf7ec..0xf7ff  _INITIALIZED   vector table + dev_list
0xf800..0xfdff  _HEAP          operating-system heap (1536 bytes)
0xfe00..0xfeff  _IM2           im 2 vector page (256 bytes)
0xff00..0xffff  _PAGE0         page-0 template + installer + early stack
```

Current verified link result:

- `_CODE` at `0xe816`, size `0x0fd2`
- `_SYSVARS` at `0xf7e8`, size `0x0004`
- `_INITIALIZED` at `0xf7ec`, size `0x0014`
- `_HEAP` at `0xf800`, size `0x0600`
- `_IM2` at `0xfe00`, size `0x0100`
- `_PAGE0` at `0xff00`, size `0x0100`

That puts the whole linked kernel image at `0xe816..0xffff`, for a total of
`0x17ea` bytes (`6122` bytes).

## File roles

### `init.s`

Defines `__sys_kernel`, the first shared-memory kernel entry.

Current behavior:

- disables interrupts with `di`
- sets `sp` to `0xffff`
- initializes the system heap with `mem_init(__sys_heap, 0x0600)`
- parks in a `halt` loop

So the first stack grows down from the top of the final page.

### `dev.s` and `list.s`

The kernel now owns the generic list helpers in `src/kernel/list.s`, while
device-list management still lives in `src/drivers/dev.s`. Neither is called
from the first kernel entry yet.

- `list.s` provides:
  - `list_match_eq`
  - `list_find`
  - `list_iterate`
  - `list_append`
  - `list_insert`
  - `list_remove`
  - `list_remove_first`

The module now exposes both:

- native assembly entry points (`list_*`) for kernel code
- SDCC `sdcccall(1)` wrappers (`_list_*`) so the same routines can be
  offered as C-callable OS services
- `dev.s` provides:
  - `dev_init`
  - `dev_probe_all`
  - `find_dev_drv`

`dev_probe_all` currently probes:

- `sio`
- `rtc`
- `nvram`
- `gdp`
- `fd`
- `hd`

The global device-list head `dev_list` currently lives in `_INITIALIZED`.

### `vectors.s`

Defines the shared-memory vector dispatch table.

- `__sys_entry` loads the reset target from `__sys_vec_entry` and jumps to it
- `__sys_vec_entry` currently defaults to `__sys_kernel`
- `__sys_vec_rst08 .. __sys_vec_rst30` default to `ret`
- `__sys_vec_rst38` defaults to `reti`
- `__sys_vec_nmi` defaults to `retn`

Only the reset entry has a shared-memory stub. The copied low-page image
directly loads all other handler addresses from the vector table with
`ld hl,(#vector)` / `jp (hl)`.

### `heap.s`

Reserves the operating-system heap in `_HEAP`.

- base: `0xf800`
- size: `0x0600` (`1536` bytes)
- range: `0xf800..0xfdff`

This heap sits directly below the dedicated IM 2 page.

### `im2.s`

Reserves the interrupt-mode-2 vector page in `_IM2`.

- base: `0xfe00`
- size: `0x0100` (`256` bytes)
- range: `0xfe00..0xfeff`

This page sits between the heap and the final kernel page/stack block.

### `mem.s` and `sysobj.s`

These provide the first kernel-owned dynamic resource layer.

- `mem.s` provides:
  - `mem_init`
  - `mem_allocate`
  - `mem_free`
  - `mem_free_owner`
- `sysobj.s` provides:
  - `so_create`
  - `so_destroy`

Both modules expose:

- native assembly entry points for kernel code
- SDCC `sdcccall(1)` wrappers so they can also be surfaced as OS services

### `src/drivers/*`

The kernel now links the driver set from `src/drivers/`:

- `sio.s`
- `rtc.s`
- `nvram.s`
- `gdp.s`
- `fd.s`
- `hd.s`
- shared helpers: `drv.s`, `delay.s`, `bcd.s`

At the moment, the linked driver set also contributes a tiny `_SYSVARS`
block:

- `gdp.s` uses 3 bytes of scratch storage for temporary device and
  character state during open/write paths

### `page0.s`

Defines the final `_PAGE0` block, which is linked at `0xff00..0xffff` and is
exactly `256` bytes long.

The file contains two parts:

1. The copied low-page image at `__sys_page0 .. __sys_page0_end`
2. The installation helper code that lives after the copied image but still
   inside the same final 256-byte kernel page

The copied low-page image currently contains:

- `RST 0x00`:
  - `di`
  - `jp __sys_entry`
  - inline metadata bytes at offsets `0x04..0x07`:
    - `__sys_version`
    - `__sys_model`
    - `__sys_flags`
    - `__sys_meta1`
- `RST 0x08 .. 0x30`:
  - `ld hl,(#__sys_vec_rstXX)`
  - `jp (hl)`
  - 4 metadata/padding bytes
- `RST 0x38`:
  - `ld hl,(#__sys_vec_rst38)`
  - `jp (hl)`
  - 4 metadata/padding bytes
- low-page info area:
  - `__sys_nvram_cache` = 8 bytes
  - `__sys_info_reserved` = 30 bytes
- `NMI 0x66`:
  - `ld hl,(#__sys_vec_nmi)`
  - `jp (hl)`
  - 1 pad byte

The tail of the final page contains:

- `page0_ret$` = 2-byte scratch return address used by
  `__sys_page0_install`
- `__sys_page0_free` = 43-byte zero-filled free tail

That free tail is available as the first tiny operating-system stack before a
larger stack policy is implemented.

## Notes

- `_PAGE0` is deliberately the last linked area so it lands at `0xff00`.
- The final page is fully materialized in the binary with explicit zero bytes.
- The top 2 KB kernel-reserved block now lives at:
  - heap at `0xf800..0xfdff`
  - IM 2 vector page at `0xfe00..0xfeff`
  - stack/page0 at `0xff00..0xffff`

## Next likely steps

- switch the ROM loader from BIOS payload boot to kernel payload boot
- replace the `halt` loop in `__sys_kernel` with real early init
- define the first permanent stack policy
- build higher-level kernel resources on top of `mem.s` + `sysobj.s`
- add system-call registration / service-query support

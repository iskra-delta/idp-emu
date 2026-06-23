# PARTOS VOLUME 3: OS

Volume 1 covers the ROM, Volume 2 covers the micro-kernel, and this volume now
describes the layer that begins once the kernel jumps into `os.sys` at
`0xC000`.

This volume is no longer about a planned OS. There is a real boot path, a real
public service table, a real shell, and the first real user-space commands.

## Current Boot Chain

The current boot flow is:

1. the ROM uncompresses itself to safe RAM, disables the ROM overlay, and
   reads the split reserved image from disk
2. the ROM loads:
   - sectors `1..8` (`2 KiB`) to `0x0000` as `kernel.sys`
   - sectors `9..72` (`16 KiB`) to `0xC000` as `os.sys`
3. the ROM jumps to the kernel page-0 entry
4. the kernel initializes memory, system objects, threads, vectors, and jumps
   into the first OS thread
5. `os.sys`:
   - caches the detected Partner model
   - snapshots the NVRAM setup block
   - installs the `rst 0x10` service-query bridge
   - wires the CTC tick into the scheduler / timer chain
   - initializes and probes drivers
   - registers the public `"partos"` service
   - mounts the boot FAT volume
   - loads `/SHELL.COM`
6. the shell later resolves `/NAME.COM` and launches commands as normal
   processes

## Reserved-Sector Image

The ROM-to-OS contract is now concrete.

The bootable hard-disk image reserves `73` sectors at the start:

- sector `0`: BPB / boot sector (`0x55AA` present)
- sectors `1..8`: `kernel.sys`
- sectors `9..72`: `os.sys`

`tools/mkdosdisk.py` is the house tool that packs those split images into the
reserved region and then builds a DOS-style FAT volume around them.

## OS Responsibilities

The OS layer now owns:

- the public `"partos"` syscall service
- console and keyboard policy above the raw drivers
- process creation and teardown
- relocatable program loading
- FAT-backed boot-volume access
- shell command resolution (`NAME` -> `/NAME.COM`)
- the first user-space tools

The kernel still owns:

- page 0 and vector plumbing
- interrupt reference counting
- memory allocator primitives
- thread scheduling and waits
- low-level device model and IM2 dispatch

## Public Service

The named service is `"partos"`.

It currently exports:

- system snapshot access (`get_sys_info`)
- console calls (`clear_screen`, `set_xy`, `write_console`)
- keyboard calls (`peek_keyboard`, `read_keyboard`)
- command launch (`run_command`)
- intrusive list helpers
- memory allocation/free
- interrupt/vector helpers
- service registration/query
- events and timers
- thread calls
- process calls
- FAT mount / lookup / open / create / read / write / readdir

User-space commands obtain this table through `rst 0x10` and then stay on the
public ABI surface. They do not call private kernel labels directly.

## Executable Format

User programs are distributed as `.COM` files.

Each `.COM` file is:

- a fixed 16-byte COM header
- one embedded relocatable `.XL` image
- optional zero padding to the chosen media/block alignment

The COM header currently carries:

- magic/version (`"CM"`, version `1`)
- requested process stack size
- entry hint for the embedded XL payload
- embedded XL offset and size
- two reserved words for future metadata

The embedded XL image carries:

- magic/version (`"XL"`, version `1`)
- linked entry offset
- code/data payload size
- relocation count
- relocation table entries
- payload bytes

The runtime path is:

1. read the whole `.COM` file into a user-heap buffer
2. validate the COM header
3. locate and validate the embedded XL image
4. relocate the XL payload in place
5. allocate a process object and a separate thread stack
6. start the process at the relocated entry point
7. transfer the image-buffer owner to the process
8. reap the process, stack, events, timers, services, and image block when the
   last thread exits

## Current Userland

The tree now ships these commands:

- `SHELL.COM`: interactive shell
- `LS.COM`: lists `/` or one chosen directory on the boot volume
- `PS.COM`: shows the live process list and main-thread states
- `MEM.COM`: prints system/user heap usage
- `CAT.COM`: dumps one aligned file
- `CP.COM`: copies one aligned file
- `MV.COM`: moves one aligned file by copy+delete
- `DEL.COM` / `RM.COM`: remove one file
- `MKDIR.COM` / `RMDIR.COM`: create/remove directories
- `TOUCH.COM`: create one empty file
- `CLEAR.COM`: clear the active console
- `ECHO.COM`: print the current argument string
- `HELP.COM`: print the current command set

The shell itself is also a relocatable COM application. It uses only the
public `"partos"` service for screen output, keyboard input, command launch,
and foreground command waiting.

## Current Limitations

The current OS is real, but it is still early.

Known limits:

- the shell has no current-directory state yet; commands are still resolved in `/`
- `ps` is intentionally minimal
- file tools still operate in 256-byte aligned FAT blocks; partial-byte file
  streaming is not finished yet
- there is still no finished `cd` / current-directory model
- the public system snapshot exposes the active shared/system heap and the
  current-bank user heap, but not a full cross-bank memory view yet
- console behavior still depends on the quality of the current emulator model,
  especially for scrolling and terminal edge cases

## What Comes Next

The immediate next OS tasks are:

1. add a real current-directory model (`cd`, relative paths, `pwd`)
2. expose the additional kernel/OS state needed for a true cross-bank memory
   viewer
3. lift the current 256-byte aligned file-I/O restriction
4. keep improving shell editing/error reporting
5. keep tightening the public ABI so future commands remain cleanly decoupled
   from kernel internals

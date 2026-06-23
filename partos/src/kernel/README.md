# kernel

The PartOS **micro-kernel** — the small, always-resident core linked at `0x0000`
and mirrored into both RAM banks. It owns scheduling, memory, events, system
objects and the interrupt plumbing, and nothing else: drivers, the filesystem,
named services, soft timers and the shell all live in the separate OS payload
(`src/os/` + `src/drivers/`, linked at `0xC000`).

Authoritative documentation: **`partos/docs/PARTOS-VOLUME-2-KERNEL.md`**.

## Current state

- Built by `make sys` as **`bin/kernel.sys`** (the OS payload is `bin/os.sys`).
- **2035-byte image, 8 sectors — fits the 2 KB low-page / mirror budget**
  (`_CODE` 1961 B + `_INITIALIZED` 74 B; `_CODE` has ~87 B free).
- Mutable runtime state (sysvars, heap, stack, IM2 table, the exec-space scratch
  for the bank routines) is reserved high in shared RAM (`0xF940..0xFFFF`) and is
  **not** part of the loaded image; the kernel sets it up at boot.
- Entered at `0x0000`: the `rst 0x00` slot is `di ; jp __sys_kernel`.

## What it has

- **rst/nmi low page** — direct `jp` dispatch; `set_vector` patches the operand
  in place (the slot *is* the table). `rst 0x08` returns `&kernel_table`.
- **scheduler** — `thread.s` + `__thread_robin`, the context switch behind the
  `rst 0x18` "tick" vector. The kernel arms **no clock**: no tick → cooperative,
  an OS timer routed at `rst 0x18` → preemptive.
- **memory** — `mem.s` owner-tracked heap; `sysobj.s` objects with per-object
  cleanup; `list.s` intrusive lists; `evt.s` events; `lock.s` spin locks.
- **interrupts** — `ir.s` reference-count bracket; `vectors.s` low-page vectors.
- **banking** — one `__bank_copy(src,dst,cnt,dir)` (parked in the rst38→nmi gap)
  + `init` brings the kernel up in *both* banks (mirror the low page, seed bank
  B's arena heap). `thread_create` records each thread's `bank` and an opaque
  `thread_data`.
- **ABI** — `kernel_table` (discover via `rst 0x08`) + `get_sys_vars` publishing
  the heaps, IM2 base, and the kernel list-head block (`include/kernel.h`).

## What it does not have (yet)

- A reworked **ROM loader** for the two split images (`make rom` is currently
  broken on the stale monolithic `kernel_loadbase.inc`).
- A **split boot probe** — the kernel builds and links, but the boot path
  (mirror → scheduler → payload) has not yet been run end-to-end.
- Cross-bank scheduling. The list heads currently sit in the mirrored low page
  (per-bank copies); they move to a single shared-RAM copy when that lands.

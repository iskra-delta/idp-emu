# PARTOS VOLUME 2: KERNEL

This volume describes the PartOS **micro-kernel**: the small, always-resident
core that owns scheduling, memory, IPC and the interrupt plumbing. Everything
else — drivers, the filesystem, named services, soft timers, the shell — lives
**above** the kernel in a separate, swappable OS payload (see Volume 3).

The guiding idea is an exokernel-style split:

- a **2 KB micro-kernel** linked at `0x0000`, mirrored into both banks, and
- a **16 KB OS payload** linked at `0xC000` in always-mapped shared RAM.

The kernel knows nothing about devices, files or shells. It brings the machine
up to a running scheduler and then hands control to whatever payload is loaded
at `0xC000`. A bare program that brings its own runtime can replace that payload
and run on the kernel alone.

## Current Milestone

At the end of the current boot milestone, the micro-kernel side is considered
**implemented and verified up to OS bootstrap**.

What works today:

- the ROM reads the **boot sector** to scratch at `0x1800`, then loads
  `kernel.sys` at `0x0000` and the OS payload at `0xC000`
- `__sys_kernel` initializes the interrupt core, heaps and scheduler
- the kernel mirrors its 2 KiB low page into the second bank
- the kernel creates the idle thread and the first payload thread
- the first payload thread enters `__os_entry` at `0xC000`
- `__os_entry` performs the minimal OS-side setup and tail-hands off to
  `_kernel_bootstrap`

What this has been verified against:

- `idp-kernel-probe` proves the direct `kernel -> __os_entry -> _kernel_bootstrap`
  path without involving the ROM loader
- `idp-full-boot-probe` proves the real `ROM -> kernel -> __os_entry ->
  _kernel_bootstrap` chain

This volume therefore describes a kernel whose boot contract is complete, but
whose job still ends at the OS bootstrap handoff. Everything after
`_kernel_bootstrap` belongs to the OS layer.

---

## 1. The Two Images

The build produces two independent, fixed-base images (`make sys`):

| Image | Source | Link base | Size today | Role |
|---|---|---|---:|---|
| `bin/kernel.sys` | `src/kernel/*.s` | `0x0000` | 2025 B (8 sectors) | the micro-kernel (this volume) |
| `bin/os.sys` | `src/os/*.s` + `src/drivers/*.s` | `0xC000` | ~14 KB | the OS payload (Volume 3) |

`kernel.sys` is currently a **2025-byte `_CODE` image** that fits inside the 2 KiB
mirror budget with room to spare. The ABI table, low-page vectors, scheduler and
the kernel's live list heads are all part of that fixed low-page image (the list
heads are tucked into dead low-page pad, see §5). Its mutable runtime RAM
(`_SYSVARS`, `_HEAP`, the IM2 table, the bank-routine exec scratch) is *not* in
the image — it is reserved high in shared RAM and is brought up at boot.

Because the two images link at constant bases, neither one's addresses drift
when the other grows. The OS resolves the kernel functions it calls directly
through an absolute-equate stub generated from the kernel link map
(`build/kernel_imports.s`); an untrusted payload instead discovers the ABI at
runtime via `rst 0x08` (see §6).

---

## 2. Memory Layout (Placement)

```
0x0000 ┌───────────────────────────────────────────────┐
       │ MICRO-KERNEL  (kernel.sys, mirrored BOTH banks)│  2 KB
       │   0x0000  rst/nmi low page (jp dispatch)       │  read-only
       │   ...     kernel _CODE  + _INITIALIZED          │  after boot
0x0800 ├───────────────────────────────────────────────┤
       │ PROCESS ARENA  (banked, per-bank)              │  ~46 KB
       │   thread stacks + process images               │  USER_HEAP
0xC000 ├───────────────────────────────────────────────┤
       │ OS PAYLOAD  (os.sys: OS + all drivers)         │
       │   ...     (payload grows upward)               │  ← must stay
       │                                                │     below 0xF940
0xF940 ├───────────────────────────────────────────────┤  ┐
       │ 0xF940  exec scratch (bank routine)  128 B     │  │ kernel
       │ 0xF9C0  sysvars                        64 B     │  │ runtime
       │ 0xFA00  system heap (shared objects)    768 B      │  │ reserve
       │ 0xFD00  stack (isr 128 + kernel 128)  256 B     │  │ (NOT loaded)
       │ 0xFE00  IM2 vector table (page-al.)   512 B     │  │
0xFFFF └───────────────────────────────────────────────┘  ┘
```

Key facts:

- **`0x0000–0x07FF` — micro-kernel.** Mirrored *identically* into both banks so
  it is effectively always mapped. It holds **no mutable data**, so it can stay
  **read-only after boot** — with one deliberate exception: the low-page `jp`
  vectors are patched by `set_vector` (see §5). Holds the scheduler, allocator,
  events, system objects, the interrupt core and the `kernel_table`.
- **`0x0800–0xBFFF` — process arena.** The banked per-process heap (`USER_HEAP`,
  base `0x0800` = just above the kernel). Thread stacks and process images come
  from here; both banks use the same window.
- **`0xC000 → 0xF9BF` — OS payload.** `os.sys` (the OS plus every driver) loads
  at `0xC000` and grows upward. It must stay **below the kernel reserve floor**
  (`KERNEL_SYSVARS_BASE` = `0xF9C0`); the build warns if it doesn't.
- **`0xF9C0–0xFFFF` — kernel runtime reserve (≈1.5 KB).** A fixed top-of-RAM
  region for the kernel's *mutable* state: sysvars, the system heap (small
  kernel + OS objects), the stack, and the page-aligned IM2 table. It is
  always-mapped shared RAM, set up by the kernel at boot, and is **not part of
  any loaded image**.

This split is a **fixed convention both the kernel and the OS know** (the
constants live in `src/partos.inc`: `USER_HEAP_*`, `KERNEL_SYSVARS_BASE`,
`KERNEL_HEAP_*`, `KERNEL_STACK_*`, `KERNEL_IM2_*`). Because the reserve is fixed
rather than negotiated, the kernel may use these addresses as constants — the OS
just has to respect the floor. If the kernel ever needs more runtime memory,
grow the reserve here and the OS automatically gets less.

---

## 3. Boot & Load Sequence

```
power-on
  └─ EPROM bootstrap
       ├─ loads kernel  2 KB    → 0x0000  (mirrored into both banks)
       ├─ loads OS     ≤14 KB   → 0xC000  (up to the 0xF9C0 reserve)
       └─ jp 0x0000                       ; enter the micro-kernel
0x0000: rst 0x00  →  di ; jp __sys_kernel  ; the low page IS the entry
            └─ __sys_kernel  (init.s)      ; kernel bring-up  (see §4)
                  └─ first thread = PAYLOAD_ENTRY (0xC000 = __os_entry)
                        └─ OS payload init thread takes over (Volume 3)
```

Notes:

- The kernel is **loaded at `0x0000` and entered at `0x0000`**. The very first
  bytes are the `rst 0x00` slot (`di ; jp __sys_kernel`), so "jump to the kernel"
  and "the reset vector" are the same thing.
- The ROM only has to load the two split images and jump to `0x0000`. The
  kernel itself performs the low-page **mirror** during `__sys_kernel` by
  copying its 2 KB image into the other bank before it starts scheduling.

---

## 4. Kernel Initialization Sequence

`__sys_kernel` (`src/kernel/init.s`) is the first kernel instruction stream.
It runs entirely with interrupts **off** and, step by step:

1. **`di`** and set the stack: `sp = KERNEL_STACK_TOP` (`0xFE00`).
2. **`__ir_init`** — initialize the interrupt core (sets the `I` register to the
   IM2 page = high byte of `KERNEL_IM2_BASE` = `0xFE`; does *not* yet `im 2`/`ei`).
3. **Interrupt bracket seeded** — `__ir_init` sets `ir_refcnt = 1`, so boot
   begins already inside the held interrupt-disable bracket and no separate
   early `_ir_disable` call is needed.
4. **Heap init** — `mem_init` for the user/process heap, then for the kernel
   heap (`__sys_heap` at `KERNEL_HEAP_BASE`, `KERNEL_HEAP_SIZE`). The allocator
   must be the first subsystem up.
5. **Create the idle thread** (`__sys_idle`) — it guarantees the run list is
   never empty, so the scheduler always has something to dispatch. It is what
   the CPU runs when every other thread is blocked (see §6).
6. **Create the payload thread = `PAYLOAD_ENTRY` (`0xC000`)** via
   `thread_create` + `thread_resume`. This is the only place the kernel reaches
   "up": it schedules the payload's entry point. The current payload begins at
   `__os_entry`, a small OS-side setup stub that caches model/NVRAM state,
   installs the `rst 0x10` service bridge, wires the timer hook and initializes
   the driver layer before tail-calling `_kernel_bootstrap`. The outer
   interrupt-disable bracket is kept held across that tail handoff, then
   released by `_kernel_bootstrap` once the first OS bootstrap thread owns the
   machine.
7. **Hand off to the scheduler** — `im 2`, mark interrupts armed, `_ir_enable`
   (which both enables interrupts and balances the early `_ir_disable`), then
   `jp __thread_robin`. The **first dispatch happens inside `__thread_robin`**:
   it selects the first runnable thread and `reti`s into it. The kernel's init
   stack is simply abandoned — there is no `halt` idle loop in `init`, because
   idling is the idle thread's job.

Crucially, the kernel installs **no timer of its own**. There is no `ir_set` of
a CTC vector and no CTC programming anywhere in the kernel — it owns no device.
The scheduler tick is the `rst 0x18` vector (§6); whether anything drives it is
the OS's decision.

---

## 5. The Low Page: rst Vectors

The low page (`src/kernel/page0.s`) is linked **first** in `_CODE` so it lands at
`0x0000` and the rst/nmi slots fall on their fixed 8-byte boundaries.

Each hardware vector is a **direct `jp <handler>`** at its real address — there
is no separate vector table. The slot *is* the table:

| Address | Slot | Default target |
|---|---|---|
| `0x00` | reset / cold entry | `di ; jp __sys_kernel` (not vectorable) |
| `0x04–0x07` | reserved | filler (machine identity is OS-owned) |
| `0x08` | `rst 0x08` | `__kernel_api` — **returns `&kernel_table`** (§7) |
| `0x10` | `rst 0x10` | `__sys_rst_default` (`ret`) — OS service-bridge slot |
| `0x18` | `rst 0x18` | **`__thread_robin`** — scheduler tick / yield (§6) |
| `0x20`–`0x30` | `rst 0x20..0x30` | `__sys_rst_default` (`ret`) |
| `0x38` | `rst 0x38` (im 1) | `__sys_rst38_default` (`reti`) |
| `0x66` | `nmi` | `__sys_nmi_default` (`retn`) |

`set_vector` / `get_vector` (`src/kernel/vectors.s`) treat the **vector id as the
slot address** (all `< 0x100`, so a byte):

```
set_vector(a = slot, de = handler)   ; patches the jp operand at slot+1/slot+2
get_vector(a = slot) -> de = handler ; reads the jp operand back
```

`set_vector` returns nothing — read a slot back with `get_vector`. Both bracket
the two-byte operand write under `di`/`ei` so an interrupt never sees a
half-patched `jp`. The C ids are in `include/vector.h` / `include/kernel.h`:
`VECTOR_RST08 = 0x08 … VECTOR_RST38 = 0x38`, `VECTOR_NMI = 0x66`.

> Consequence: because handlers are patched into the low page, the low page is
> self-modified by `set_vector` (it is not read-only after boot), and once
> banking is on, `set_vector` must patch the operand in **both** bank mirrors.
> `set_vector` is cold, so this is cheap.

The scheduler tick lives in this table too — `rst 0x18 → __thread_robin` — and
is the subject of the next section. A separate **soft-timer hook**
(`__sys_vec_tick`, dispatched by `__tick_dispatch`) is *not* a vector: it is an
indirect cell that `__thread_robin` calls on every tick, into which the OS's
timer driver stores its chain routine.

---

## 6. Scheduling: the `rst 0x18` Tick

The kernel owns **no clock**. The scheduler context switch, `__thread_robin`,
sits behind the `rst 0x18` vector, and the kernel never fires it. Three things
reach it, all through the same `0x18` slot:

- a **thread yielding** — `wait_events` blocks by issuing `rst 0x18` (it does
  *not* `halt` and wait for a tick); the idle thread does the same after each
  `halt`;
- the **OS routing a timer interrupt** at `0x18` — point a periodic timer's IM2
  vector at the rst18 slot and `__thread_robin` runs as that timer's ISR.

This gives a clean dial:

| Timer wired at `rst 0x18`? | Behaviour |
|---|---|
| no | **cooperative / single-tasking** — threads run until they block or yield |
| yes | **preemptive / multitasking** — the timer slices the running threads |

Because `rst` pushes the return PC exactly like an interrupt acknowledge does,
`__thread_robin`'s saved-context layout is identical whether it is entered by a
software `rst 0x18` (yield) or a hardware timer vectored at `0x18` (preempt).
One handler, both worlds.

### The idle thread

`init` creates a tiny kernel **idle thread** (`__sys_idle`) so the run list is
never empty and the scheduler always has something to dispatch. It is the
lowest-effort fallback: `halt` until any interrupt, then `rst 0x18` to re-enter
the scheduler and pick up whatever just became runnable. With no timer wired,
this is the entire idle/wake loop of a single-tasking system (an I/O completion
ISR signals an event, idle wakes, re-selects, and the unblocked thread runs);
with a timer wired, idle is simply preempted like any other thread.

### Driving it from the OS (CTC)

Routing the tick is **OS policy**, not a kernel concern. The OS owns the CTC
driver (`src/drivers/ctc.s`); to enable preemption it programs a CTC channel for
a periodic interrupt and points that channel's IM2 vector at `0x18`. The kernel
contains no CTC code at all — that is the whole point of moving the tick onto a
vector.

---

## 7. Calling the Kernel via `rst 0x08`

The kernel exposes its entire ABI as one read-only function-pointer table,
`kernel_table`, whose layout is the `kernel_t` struct in `include/kernel.h`. A
caller obtains a pointer to it by issuing **`rst 0x08`**:

```
            rst     0x08            ; hl = &kernel_table  (via __kernel_api)
```

That is the only thing `rst 0x08` does: it returns `hl = &kernel_table`. From
there a caller indexes the table and calls through the slot. Entry 0 is
`get_sys_vars`; each entry is a 2-byte pointer:

```
            rst     0x08            ; hl -> kernel_table
            ;; call get_sys_vars() (table entry 0)
            ld      e,(hl)
            inc     hl
            ld      d,(hl)          ; de = &get_sys_vars
            ex      de,hl
            ld      de,#ret$        ; push a return address...
            push    de
            jp      (hl)            ; ...and tail into the function (call-by-jp)
ret$:
            ;; de/hl now hold the sysvars_t* per the kernel ABI
```

In C the same thing reads:

```c
#include "kernel.h"
const kernel_t *k = kernel_api();         /* rst 0x08 -> &kernel_table */
sysvars_t *sv = k->get_sys_vars();
void *p = k->allocate_memory(sv->sys_heap, 64, NONE);
```

**Two callers, two paths to the same code:**

- The **trusted OS** is linked alongside the kernel and always mapped, so it may
  also call kernel functions **directly by their linked address** (resolved
  through `build/kernel_imports.s`). It does not need `rst 0x08`, though the
  table is identical either way.
- An **untrusted / bare payload** uses `rst 0x08` so it needs *no* compile-time
  knowledge of kernel addresses — only the stable table layout.

### Kernel ABI surface (`kernel_table`)

The table, in order (see `include/kernel.h` and `src/kernel/kernel.s` — keep the
two in lock-step):

- **system vars:** `get_sys_vars`
- **memory:** `allocate_memory`, `deallocate_memory`
- **events:** `create_event`, `destroy_event`, `set_event`, `is_signalled`
- **lists:** `insert_list`, `remove_list`, `find_list`, `iterate_list`,
  `match_list_eq`
- **threads:** `create_thread`, `resume_thread`, `suspend_thread`,
  `exit_thread`, `wait_events`
- **system objects:** `create_object`, `destroy_object`, `set_cleanup`,
  `register_owner_cleanup`
- **vectors:** `set_vector`, `get_vector`
- **locks:** `acquire_lock`, `release_lock`, `test_lock`

`get_sys_vars()` returns a `sysvars_t*` the kernel publishes: the three heap
bases, the IM2 table address, and the live addresses of the kernel's list heads
(threads current/suspended/running/waiting/terminated, and the event list). The
OS writes its declared heap bases back into it.

Named **services** and soft **timers** are deliberately *not* in this table —
they moved to the OS. The kernel is discovered via `rst 0x08`; it never
registers a service for itself, and it never wires the CTC tick to a timer
chain (that is OS policy).

### Calling convention

All kernel asm entry points use `sdcccall(1)`: first argument in `HL` (or `A`
for a 1-byte first arg such as the `set_vector` slot), second in `DE`, the rest
on the stack (callee-cleaned); 16-bit/pointer results return in `DE`.

---

## 8. Building Blocks

The kernel ships the primitives the OS builds everything else on:

- **`list.s`** — intrusive single-linked-list helpers (`list_insert`,
  `list_remove`, `list_find`, `list_iterate`, `list_match_eq`, …). `next` is
  always the first field of any listable struct.
- **`mem.s`** — `mem_init`, `mem_allocate`, `mem_free`, `mem_free_owner`
  (owner-tracked heap).
- **`sysobj.s`** — `so_create` / `so_destroy` over the heap, with a per-object
  cleanup hook (`set_cleanup`) run by `so_destroy` and by the owner-death sweep.
- **`evt.s`** — sync events (the block/wake primitive).
- **`thread.s`** — cooperative + preemptive threads and the `__thread_robin`
  context switch.
- **`ir.s` / `vectors.s`** — the interrupt reference-count bracket and the
  low-page vector management.
- **`lock.s`** — spin locks over a one-byte cell.

---

## 9. Interrupt Topology

- IM2 is used. The `I` register holds the IM2 page (high byte of
  `KERNEL_IM2_BASE` = `0xFE`); a device vector `V` selects the handler pointer at
  `0xFE00 + V`. The kernel sets up the page and enables `im 2`; the **OS**
  installs the actual device ISRs.
- The kernel programs **no interrupt source of its own**. The scheduler tick is
  the `rst 0x18` vector (§6), driven by a thread yielding or by an OS timer
  whose IM2 vector points at `0x18`. Historically the kernel armed CTC channel 3
  (the AVDC ~50 Hz vertical blank) itself; that is now the OS CTC driver's job.
- Daisy-chain priority: DMA › CTC › SIO0 › SIO1 › PIO. The i8272 floppy
  controller is separate (external vector latch).
- ISRs hold the `ir_refcnt` bracket across their body and do a manual `ei ; reti`
  — see the interrupt notes in the source.

---

## 10. Assembly Style

The tree has a deliberate, consistent style — please keep it:

- labels start in column 0; instructions/directives/standalone comments at
  column 12
- mnemonics and registers lowercase; area names uppercase
- public symbols use `::`; local labels end in `$`
- SDAS immediates use `#`
- routine headers document inputs, outputs and destroyed registers

Consistent layout is one of the cheapest ways to keep low-level work readable.

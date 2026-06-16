# Note: Design Decisions and Rationale

Category: Architecture / Decisions Log
Date(s): 2026-06-11

Chronological log of design decisions, with short rationale. The full
picture lives in [../partos/ARCHITECTURE.md](../partos/ARCHITECTURE.md).

## Decisions Log

- 2026-06-11 — Project structure created:
  `partos/{src,include,docs,build,bin}`.
- 2026-06-11 — Core memory architecture decided: 2 KB ROM holds boot +
  BIOS; BIOS replicated into both banks at 0x0000; ROM overlay disabled
  after replication; all BIOS variables in common RAM at top of memory.
- 2026-06-11 — **Revised** (supersedes the above): only **page 0**
  (0x0000-0x00FF, RST/NMI stubs) is replicated into both banks; the BIOS
  proper (code + variables) moves to the **top of common RAM**. If the
  BIOS fits the 2 KB ROM it is copied from ROM at boot; otherwise the ROM
  acts as a boot loader and loads it from disk. The OS owns
  0x0100-0xBFFF in both banks.
- 2026-06-11 — Notes reorganized from a single NOTES.md into categorized
  files under `docs/notes/`.
- 2026-06-11 — Driver format settled: single global device list of dev_t
  (30 bytes, data[16] holds driver-private state incl. block position);
  driver open() receives the resolved dev_t*; find_dev_drv(name) iterates
  the device list and returns the driver via the back pointer (asm also
  returns the dev_t* in de). Instances are sbrk-allocated by probe();
  heap is 512 bytes at 0xFE00-0xFFFF growing down (note: displaces the
  old IM2-table-at-0xFE00 idea).
- 2026-06-11 — List convention: the next pointer is the FIRST member of
  every list-able structure (dev_drv_s, dev_s, list_s). Generic
  list.s/list_append handles all lists uniformly (head variable and next
  field are identical cells); probe results are appended to the device
  list with list_append (dev_add removed). C view in include/list.h.
- 2026-06-11 — ROM split: page 0 (vectors + cold init) at 0x0000, boot
  (_BOOT, the boot-sector loader) at 0x0100 runs FROM ROM in place; only
  the BIOS (_CODE) is copied to memory top (0xF600), stored in ROM right
  after boot. No size constants anywhere: page 0 init copies via linker
  area symbols (s__BOOT/l__BOOT/s__CODE/l__CODE), and the Makefile reads
  link bases from partos.inc and segment sizes from the link map; the
  2048-byte pack and overflow check are automatic. bios.s (jump tables,
  page-0 bank install, init) comes later.
- 2026-06-11 — Device instances are STATIC, not heap-allocated
  (supersedes "instances are sbrk-allocated by probe()"): each driver
  declares its dev_t structures in its own module; they are part of the
  BIOS image and therefore writable RAM at runtime. probe() only chains
  the units actually present via their next fields and returns the chain;
  list_append() hooks it into the global list. No duplicate storage, no
  copy. The sbrk heap remains for other runtime needs.

## Rationale (short form)

1. **Replicate only page 0 (256 B) to both banks** → the Z80's hardwired
   entry points (RST 0x00..0x38, NMI 0x66) are valid in every bank; the
   stubs jump into common RAM. Only 256 bytes per bank spent; the OS owns
   0x0100-0xBFFF in each bank.
2. **BIOS proper (code + variables) at top of common RAM** → exists
   exactly once, visible in every bank; interrupt handlers and BIOS calls
   are bank-agnostic by construction; trivially patchable. Page-0 code is
   the same in both banks, BIOS code/data is shared.
3. **No ROM overlay at runtime** → the whole low 8 KB window becomes
   normal RAM; ROM is only a delivery vehicle for the boot + BIOS image.
4. **BIOS too big for 2 KB? Boot it.** The ROM then carries page 0 + boot
   + minimal boot-device driver and loads the BIOS from disk to the same
   top-of-memory location. Runtime layout unchanged.

## Known Trade-off

The BIOS competes with the OS for **common RAM** (16 KB total, the
scarcest resource in the system). The budget for how much of it the BIOS
may claim (code + data + stack) should be pinned down early — see
[OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).

# Note: PartOS Memory Layout (historical plan)

Category: Architecture / Memory
Date(s): 2024-04-14, 2026-06-11 (merged from `2024-04-14_mem-layout.md`)

Original 2024 memory layout plan. The current, decided architecture lives
in [../partos/ARCHITECTURE.md](../partos/ARCHITECTURE.md) — it follows the same shape
(page 0 low, kernel/BIOS at the top of common RAM) with the addition of
page-0 replication into both banks; exact top-of-memory boundaries are
still to be fixed (see [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md)).

## 2024 plan

PartOS requires a custom ROM that includes the PartOS microkernel, and the
PartOS OS loader. At start up the microkernel is copied to the non-banked
RAM and the loader loads and runs the boot sector.

~~~
       +-----------------------+
0x0000 |         page 0        |    256 bytes
       +-----------------------+
0x0100 |                       |
       |                       |
       |                       |
       |      banked heap      |    48896 bytes
       |                       |
       |                       |
       |                       |
       +-----------------------+
       |                       |
0xc000 |    non-banked heap    |    12736 bytes
       |                       |
       +-----------------------+
       |                       |
0xf1bf |       u-kernel        |    2048 bytes
       |                       |
       +-----------------------+
0xf9bf |    sys. vars (64b)    |    64 bytes
0xf9ff +-----------------------+
       |     1kb os stack      |    1024 bytes
       +-----------------------+
0xfdff | interrupt vector 0x00 | \
       +-----------------------+  |
       |          ...          |  | 512 bytes
       +-----------------------+  |
0xfffd | interrupt vector 0xff | /
       +-----------------------+
~~~

## Observations (2026-06-11)

- The 512-byte IM2 vector table at the very top (0xFE00-0xFFFF, I=0xFE/0xFF)
  is a concrete answer candidate for the open interrupt-mode question:
  IM2 with the table in common RAM, writable via set/get-interrupt-vector
  system calls.
- The 2024 plan put the u-kernel at 0xF1BF (2 KB) with sys vars + stack +
  vector table above it — total top-of-memory claim ~3.6 KB of the 16 KB
  common area. A similar budget likely applies to the new BIOS-proper
  region.

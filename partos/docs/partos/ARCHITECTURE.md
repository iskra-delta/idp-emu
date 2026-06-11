# PartOS Architecture

PartOS is an operating system for the Iskra Delta Partner (Z80A, 4 MHz).
This document describes the core design decisions. Working notes are
categorized under [notes/](../notes/README.md): the decisions log, open
questions, hardware quirks, the I/O map and emulator-support notes.

## Design Summary

- The ROM is **2 KB** (0x0800 bytes) and contains the boot process and, if
  it fits, the complete BIOS.
- At runtime, the BIOS is split in two parts:
  - **Page 0** (0x0000 - 0x00FF) is **replicated into both RAM banks**. It
    holds the Z80-reserved entry points — the RST 0x00..0x38 vectors and
    the NMI handler at 0x0066 — as short stubs that jump into the BIOS
    proper. The OS owns everything above it.
  - **The BIOS proper** (code + all variables) lives at the **top of
    common RAM** (0xC000 - 0xFFFF), which is always mapped regardless of
    the active bank.
- The ROM overlay is switched off permanently once this layout is
  established.
- If the BIOS outgrows the 2 KB ROM, the ROM becomes a boot loader that
  establishes the same runtime layout by **loading the BIOS from disk**
  into the top of memory. The runtime architecture does not change —
  only where the BIOS bytes come from.

The result: **all interrupt entry points and BIOS functions are available
to the OS at all times, in every bank.** Page-0 code is identical in both
banks AND the BIOS code/data is shared in common RAM.

## Hardware Memory Model

The Partner banking hardware divides the 64 KB address space:

| Range           | Size  | Behavior                                      |
|-----------------|-------|-----------------------------------------------|
| 0x0000 - 0xBFFF | 48 KB | Banked: bank 1 or bank 2 selected at a time   |
| 0xC000 - 0xFFFF | 16 KB | Common: always visible, shared by both banks  |

Banking is controlled by *touching* I/O ports (a read **or** a write both
trigger the switch):

| Port touch  | Effect                                  |
|-------------|------------------------------------------|
| 0x80 - 0x87 | Disable ROM overlay (one-way; see notes) |
| 0x88 - 0x8F | Select RAM bank 1 (reset default)        |
| 0x90 - 0x97 | Select RAM bank 2                        |

While the ROM overlay is enabled (it is at reset), the 2 KB ROM is mirrored
4x across **0x0000 - 0x1FFF**. Writes into that window are lost while the
overlay is on. Disabling the overlay exposes the full RAM of the selected
bank.

## PartOS Memory Map (runtime)

```
        bank 1                       bank 2
0x0000  +--------------------+       +--------------------+
        | page 0 (RST/NMI)   |   ==  | page 0 (RST/NMI)   |  identical 256 B
0x0100  +--------------------+       +--------------------+
        |                    |       |                    |
        |                    |       |                    |
        |   OS / user space  |       |   OS / user space  |
        |     (per bank)     |       |     (per bank)     |
        |                    |       |                    |
0xC000  +--------------------+-------+--------------------+
        |          common RAM: OS code/data (shared)      |
        +-------------------------------------------------+
        |   BIOS proper: code + variables + system stack  |
0xFFFF  +-------------------------------------------------+
```

- **0x0000 - 0x00FF (both banks): page 0.** Byte-for-byte identical in
  both banks. RST and NMI entries are stubs (`JP biosfn` into the BIOS
  proper). This is the only memory the BIOS claims inside the banked
  region.
- **0x0100 - 0xBFFF (per bank): OS / user space.** Nearly the entire
  banked range belongs to the OS — 47.75 KB per bank.
- **0xC000 - 0xFFFF: common RAM.** Shared between the OS and the BIOS.
  The BIOS proper sits at the very top: code, then all BIOS variables, and
  the system stack. Exact boundaries are fixed once BIOS code/data sizes
  are known; the region grows down from 0xFFFF, and everything below it in
  the common area is the OS's.

### Why only page 0 is replicated

The Z80 hardwires its entry points low: RST n pushes to 0x0000..0x0038 and
NMI to 0x0066. Those addresses are in the *banked* region, so they must be
valid in **every** bank — hence one identical 256-byte page per bank.
Nothing else needs to be: the stubs immediately jump into common RAM,
which is mapped no matter what.

Compared to replicating a full low-memory BIOS, this:

- costs 256 bytes per bank instead of 2 KB per bank;
- keeps exactly **one** copy of BIOS code and data (no risk of the two
  copies diverging, trivially patchable at runtime);
- gives the OS an almost-complete banked address space starting at 0x0100
  (the classic layout CP/M-style systems expect).

### Why the BIOS proper lives in common RAM

- It is visible in every bank, so BIOS calls and interrupt handlers never
  care which bank was active when they were invoked.
- A bank switch can happen at any point — even between a page-0 stub
  and its target — and nothing breaks: page 0 is identical in both banks,
  and the target is in always-mapped memory.
- Code and data sit together at the top of memory; no cross-bank data
  access rules are needed. Rule:

> **Page 0 is replicated; everything else about the BIOS — code and data —
> exists exactly once, at the top of common RAM.**

## Boot Sequence

At reset: ROM overlay enabled, bank 1 selected, execution starts at 0x0000.

1. **Minimal init** — set SP into top of common RAM, basic device setup
   needed for early console/diagnostics.
2. **Install BIOS high** — copy the BIOS image from ROM to the top of
   common RAM (common RAM is writable even while the ROM overlay is on).
   *If the BIOS doesn't fit the ROM:* initialize the boot disk device and
   load the BIOS image from disk to the same place instead.
3. **Jump high** — transfer control to the relocated BIOS, now executing
   from common RAM. The ROM overlay is no longer being executed.
4. **Disable ROM & install page 0** — touch port 0x80 (overlay off,
   one-way). Write the 256-byte page-0 image to 0x0000 in bank 1, touch
   port 0x90, write it to 0x0000 in bank 2, touch port 0x88 to return to
   bank 1.
5. **BIOS init** — initialize the BIOS data area and system stack at the
   top of common RAM, set interrupt mode and vectors, initialize devices
   (SIO console, CTC, FDC, SASI).
6. **Boot the OS** — use the BIOS disk drivers to load PartOS and transfer
   control to it.

ROM disable is **one-way** (only a hardware reset re-enables the overlay),
which is fine: after step 4 the ROM is never needed again.

## Interrupts

- All Z80 entry points (RST 0x00..0x38, NMI 0x66) are page-0 stubs, valid
  in every bank, dispatching into handlers in common RAM.
- Handlers run on the system stack in common RAM and touch only common-RAM
  variables, so they are bank-agnostic by construction.
- Interrupt mode (IM1 vs IM2 with a vector table) is decided with the
  driver design. Both work in this layout; an IM2 vector table would live
  in common RAM (always visible) with the I register pointing at it.

## BIOS Interface

- BIOS functions are exposed through **RST stubs in page 0** (fast,
  1-byte calls for the hottest entry points) and/or a **jump table at a
  fixed address** at the start of the BIOS proper in common RAM — exact
  split to be decided with the driver format.
- The disk driver interface (the driver format) is specified separately
  and will be documented in `docs/` when defined.

## Size Budget

The ROM must fit in 2048 bytes:

| Component                                   | Budget (working estimate) |
|---------------------------------------------|---------------------------|
| Page-0 image (RST/NMI stubs, reserved area) | 0.25 KB                   |
| Boot (install high, page-0 setup, OS load)  | ~0.3 KB                   |
| Console driver (SIO)                        | ~0.3 KB                   |
| Floppy driver (i8272 + DMA)                 | ~0.6 KB                   |
| Hard disk driver (SASI/Xebec)               | ~0.4 KB                   |
| Reserve                                     | ~0.2 KB                   |

These are planning numbers. **Decision point:** if the BIOS proper (with
its drivers) no longer fits next to the boot code, the ROM keeps only
page 0 + boot + the minimal driver needed to read the boot device, and the
full BIOS is loaded from disk (boot-time choice, not an architecture
change). The runtime memory map stays exactly as drawn above either way.

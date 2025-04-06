# Pattern: Partner Startup and Boot Process

Category: ROM / Bootloader / CP/M Loader  
Date(s): 2021-03-21 to 2021-05-27

## Problem / Purpose

Understand the full startup process of the Iskra Delta Partner computer, as implemented in ROM. Analyze how the system boots from floppy or hard disk, how CP/M is loaded, and how the BIOS/loader code behaves. Explain what happens from reset to CP/M execution.

## Findings / Observations

### Overview of Startup Flow

1. **Reset Entry Point**: ROM starts execution at address `0x0000`, which immediately jumps to the main startup routine at `0x016D`.

2. **Main Startup Routine (`0x016D`)**:

   - Initializes the stack pointer to `0xFFC0`.
   - Initializes PIO and GDP (graphics display processor).
   - Resets AVDC (text video controller) and calls `AVDCInit1` and `AVDCInit2`.
   - Initializes SIO channels for CRT (keyboard), LPT, and VAX.
   - Prints GDP startup messages and performs a memory test.
   - Initializes FDC.
   - Waits for keyboard input. If no key is pressed, it attempts **hard disk boot**. If key is available, prompts user for **disk selection (A or F)**.

3. **Input Handling**:
   - Character is read and converted to uppercase.
   - If `A`, jumps to hard disk boot routine
   - If `F`, jumps to floppy boot routine
   - Otherwise, prints `?` and re-prompts.

### Floppy Boot (FDBoot at `0x04D4`)

1. Calls `load CP/M loader` routine:

   - Loads sectors from floppy into RAM at `0xE000`.
   - Verifies the first byte of the loader is `0xC3` (JP) or `0x31` (LD SP).

2. If valid, jumps to loader using a trampoline at `0xF600`.

### Hard Disk Boot (HDBoot at `0x0546`)

1. Calls `load CP/M loader`, which:

   - Issues a test drive ready command to the Xebec S1410 controller.
   - Loads sectors into RAM at `0xE000`.
   - Validates the first byte (must be `0x31`).

2. On success:
   - Copies the entire ROM from `0x0000` to `0x07FF` to `0x2000`.
   - Copies the interrupt handler (starting at `0x0761`) to `0xC000`.
   - Sets interrupt vector to point to `0xC000`.
   - Sets up CTC, enables interrupts.
   - Jumps to CP/M loader via trampoline.

### CPMLDR Loader

- Located at `0xE000`, then copied to `0x0100`.
- Emulator expects first byte to be `0x31` (LD SP instruction).
- Suspected loader size: `0x1F00` bytes (TBD).
- Code at `0xF600` copies loader from `0xE000` to `0x0100` before jumping.
- Uses 128-byte virtual disk blocks.
- Emulator warns to avoid using RAM addresses between `0x0B00` and `0x0B72`.

### Emulator Notes

- The emulator mimics a virtual disk controller that reads only, and doesn't fully simulate hardware write operations.
- Addresses `0x0B00–0x0B72` are unstable due to emulated device buffer behavior.

## Analysis / Interpretation

- The ROM is carefully organized to support both floppy and hard disk boot, providing a flexible startup path.
- CP/M loader is treated as a relocatable binary copied to `0x0100`, the canonical CP/M application entry.
- The hardcoded check for opcode `0x31` ensures that the CPMLDR begins with `LD SP,nn`.
- The memory copy from ROM to `0x2000` ensures that utility routines are available even after CP/M has taken control.
- Interrupt handling is relocated and patched into high memory (`0xC000`).

## Solution / Summary

The Iskra Delta Partner boots by:

1. Initializing devices and video system.
2. Displaying messages and running a memory test.
3. Waiting for user input or defaulting to hard disk boot.
4. Loading CPMLDR from floppy or HDD to `0xE000`.
5. Copying loader to `0x0100` and jumping to it.
6. CPMLDR then takes over, eventually loading CP/M.

Understanding this sequence is critical for developing a compatible emulator or diagnostic firmware.

## Links

- [Matej Horvat's Partner ROM disassembly](https://github.com/matejhorvat/partner-rom-disasm)
- [CP/M on a new computer – CPUville](http://cpuville.com/Code/CPM-on-a-new-computer.html)
- [CP/M BIOS Installation – S100 Computers](http://www.s100computers.com/Software%20Folder/CPM3%20BIOS%20Installation/CPM3%20HDISK%20BIOS%20Software.htm)

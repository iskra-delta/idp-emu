# Pattern: i8272 FDC Command/Result Handshake

Category: Intel 8272 / FDC / DMA / Partner motherboard
Date(s): 2026-03-20, verified 2026-08-26

## Problem / Purpose

Record the controller contract obtained by comparing Intel's programming
manual and sample driver with the Partner motherboard wiring and both original
ROMs. This is the regression basis for command/result timing, DMA completion,
and boot compatibility.

## Findings / Observations

- ROM helper (`send_fdc_cmd`):
  - loops on `IN (F0h)` until `(MSR & 0xC0) == 0x80` (RQM=1, DIO=0)
  - then writes command/data to `F1h`
- ROM helper (`read_fdc_result`):
  - loops until `(MSR & 0xC0) == 0xC0` (RQM=1, DIO=1)
  - then reads from `F1h`
- Boot path uses:
  - reset/sense
  - specify
  - seek/recalibrate with `EI; HALT` wait
  - read-data + result-phase parsing

The motherboard part marked `8272` is an Intel 8272/uPD765A-compatible floppy
controller, not an “8782”.

## Motherboard DMA and terminal count

The schematic adds two pieces of behavior around the bare controller:

- FDC `DRQ+` sets the E107 request latch. E34 presents the latched request to
  Z80 DMA `RDY`, and the complementary latch output plus `BUSACK` produces
  active-low `DACK`.
- FDC pin 16 `TC` is E108 pin 11. E108 ANDs `BUSAK+` with the E37-inverted
  `INT1-` output of the Z80 DMA. Thus the DMA end-of-block pulse becomes
  terminal count only while DMA owns the bus.

The emulator now drives `i8272_terminal_count()` for the last byte of a DMA
block and gates DMA ready with `i8272_drq()`. MSR bit 5 is Intel's `NDM`
(non-DMA execution), so it is clear during Partner DMA transfers and set only
after `SPECIFY` selects non-DMA mode.

Intel documents that a DMA read/write normally ends on terminal count, not
merely because the `EOT` value was reached. The Partner G ROM also exposes a
board-timing detail: it programs `EOT` equal to the one sector being read and
accepts `ST1.EN = 80h` as success. Partner P instead programs `EOT = 18` and
checks the normal-termination field of ST0. The board model preserves both
real-ROM observations: terminal count completes the transfer, and the delayed
Partner EOB-to-TC path leaves EN visible when the last sector is EOT without
turning ST0 into an error.

## Controller behavior covered

- All command and result bytes use MSR `RQM`/`DIO` handshakes.
- `SPECIFY` controls DMA/non-DMA mode; `SENSE DRIVE STATUS` exposes ready,
  two-sided, track-zero, write-protect, and fault inputs.
- `SEEK` and `RECALIBRATE` set the selected drive-busy MSR bit until their
  delayed interrupt; `SENSE INTERRUPT STATUS` returns ST0 and PCN.
- Read/write commands advance through sectors, honor the N/DTL transfer-size
  rules, and end through terminal count. Missing TC at EOT has a separate
  regression.
- Read track, normal/deleted read and write, read ID, format track, and all
  three scan commands are decoded and exercised. Raw Partner images do not
  preserve deleted-data marks, CRC fields, gap bytes, or index timing, so those
  magnetic-media attributes cannot be reconstructed from the image format.
- The controller supports the full N-encoded sector buffer range through
  8192 bytes; mounted Partner media report N=1 (256-byte sectors).
- DMA execution exposes DRQ; non-DMA execution exposes NDM and is serviced
  through the data register. The model does not claim cycle-exact per-byte
  non-DMA interrupt deadlines because Partner software uses the wired DMA path.
- Completion IRQ and result phase remain separate, and result consumption
  returns the controller to command phase.

## Source and regression anchors

- [Intel iSBC 208 User's Manual](https://bitsavers.trailing-edge.com/pdf/intel/iSBC/143078-001_iSBC_208_Users_Manual_Oct81.pdf)
  contains the Intel 8272 programming chapter and a complete host driver.
- [Intel iSBX 218A Hardware Reference](https://bitsavers.trailing-edge.com/pdf/intel/iSBX/145911-001_iSBX_218_Flexible_Diskette_Controller_Hardware_Reference_Aug83.pdf)
  supplies the programming cautions used by the regressions, including EOT/TC
  and the original scan-command silicon warning.
- [Intel 8272 datasheet transcription](https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/Intel/I8272.doc)
  was compared for the pin definitions, phase state machine, command table,
  N/DTL rule, and result-bit meanings.
- `disk_controllers_unit` covers the manufacturer command streams and error
  cases. `z80dma_unit` covers the DMA programs embedded in both Partner ROMs.
  The Partner P and Partner G boot integrations prove both original ROMs load
  their CP/M system images through the corrected board path.

The controller-to-host byte contract is covered. Flux transitions, rotational
latency, index timing, CRC generation, gap layout, and deleted marks require a
track/flux image backend before they can be called cycle-exact.

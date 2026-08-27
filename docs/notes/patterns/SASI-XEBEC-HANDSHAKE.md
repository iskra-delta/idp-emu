# Pattern: SASI/Xebec S1410 Handshake

Category: Xebec S1410 / SASI / HDD Boot  
Date(s): 2026-03-20, verified 2026-08-26

## Problem / Purpose

Record the Xebec S1410 bus and command contract obtained from the controller
manual, its complete Z80 host example, the Partner SASI schematic, and both
original ROM command streams.

## Findings / Observations

- CRT ROM HDD path frequently polls port `0x10` (status/control handshake) and uses:
  - `0x11` for data
  - `0x12` for reset/control
- Common loops:
  - waiting for BSY assertion after select
  - waiting for REQ transitions before byte exchange
- If REQ/BSY timing is wrong, Partner P firmware stalls at loops around:
  - `0x05C2` (BSY wait)
  - `0x05CE` (REQ wait)

## Adapter latch and status bits

The adapter's U11 74LS174 control latch connects:

| Host bit | Schematic signal | Meaning |
| --- | --- | --- |
| D5 | `DRQ_ENB` | permit the SASI request to reach expansion DMA request |
| D1 | `EN_DATA` | enable the bidirectional data path |
| D0 | `SEL` | select the target |

Port `10h` returns D7 `REQ`, D6 `IO`, D5 `MSG`, D4 `CD`, and D3 `BSY` through
U7. Port `11h` transfers data, and any write to function 2 resets the target.
A2/A3 are not decoded, so the four functions repeat through `10h..1fh`.

## S1410 phases

- Session/select lifecycle should match SASI sequencing:
  1. assert select
  2. target asserts BSY
  3. REQ-driven command/data byte transfers
  4. target data-in or data-out, if the command has a data phase
  5. one completion-status byte with `CD=1`, `IO=1`, `MSG=0`
  6. one null message byte with `CD=1`, `IO=1`, `MSG=1`
  7. target releases `BSY`

BSY remains asserted for the complete transaction. Every Device Control Block
is six bytes. The first completion byte uses bit 1 for error and bit 5 for the
logical unit; the second byte is zero.

The original ROM startup sequence is now reproduced literally:

```text
0c 00 00 00 00 00             initialize drive characteristics
01 32 04 00 80 00 40 0b       306 cylinders, 4 heads, W=128, P=64, ECC=11
01 00 00 00 00 00             recalibrate
08 00 00 00 1f 00             read 31 blocks from LBA 0
```

The old emulator completed `0Ch` after the DCB and accidentally interpreted
the eight characteristic bytes as later commands. The controller now enters a
real eight-byte data-out phase. Command `01h` is likewise a six-byte
recalibrate DCB, not a one-byte shortcut.

Class-0 read/write uses a 21-bit LBA and a zero count represents 256 blocks.
Request Sense (`03h`) returns four bytes containing error type/code and the
failing 21-bit address. Invalid commands return an error instead of silent
success. Revision-E sector-buffer commands and documented diagnostics are
covered as well.

## Sources and verification

- [Xebec S1410 Owner's Manual](https://bitsavers.trailing-edge.com/pdf/xebec/Xebec_S1410/104524C_S1410Man_Aug83.pdf)
  is the behavioral authority. Its appendix includes a complete Z80 program
  which selects the controller, tests ready, recalibrates, formats, writes,
  reads, requests sense after error, and consumes both completion bytes.
- The Partner P and Partner G ROM annotations confirm the initialization and
  31-block boot read shown above.
- `disk_controllers_unit` covers selection, all bus-phase status bits,
  initialization data, read/write, status/message completion, invalid command,
  and request-sense behavior. Both hard-disk integration tests boot the
  corresponding CP/M image and execute a command through its real disk BIOS.

The byte protocol and Partner image behavior are verified. Mechanical seek,
rotational delay, ECC correction, physical format headers, and bad/alternate
track remapping are not represented by the flat 256-byte-block image format.

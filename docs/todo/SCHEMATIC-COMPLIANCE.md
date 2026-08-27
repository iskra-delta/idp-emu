# Schematic compliance implementation plan

Sources are the pinned motherboard, SASI, CRT, and GDP KiCad reconstructions
listed in `docs/notes/schematics/README.md`, together with the EF9367 and Zilog
datasheets. The CRT card remains a serial-terminal abstraction because its
local 8085 bus is not connected to the motherboard CPU bus.

The work is complete only when both original ROM variants still reach their
boot prompts and both Partner P and Partner G complete their CP/M floppy and
hard-disk integration tests.

## Motherboard bus and interrupt wiring

- [x] Change the interrupt priority to CTC, DMA, SIO1, SIO2, motherboard PIO,
      discrete FDC logic, then the expansion-card interrupt device.
- [x] Model the FDC interrupt latch as a daisy-chain participant with
      acknowledge, in-service blocking, and RETI release.
- [x] Connect the GDP card PIO after the motherboard FDC interrupt logic.
- [x] Feed the MC14411 `XX1` source into CTC channel 0 while preserving the
      channel 0 to channel 1 motor-timeout cascade.
- [x] Trace E67 pins 7/22 as the direct ZC/TO0-to-CLK/TRG1 net and verify the
      E91/E94/E107 ZC/TO1 motor-latch clear path with a board regression.
- [x] Trace DMA RDY through E34/E37/E107, distinguish E33 `RDY` pin 25 from
      `CE/WAIT` pin 16, and model byte/burst/continuous bus release correctly.
- [x] Trace FDC E56 pin 16 through E108/E37 to DMA `INT1-` plus `BUSAK+`, and
      terminate 8272 DMA commands from that terminal-count path.
- [x] Route the GDP card's conditioned `AVDINT-` output, rather than raw AVDC
      vertical blank, to the optional CTC channel 3 connection.
- [x] Generate the RTC alarm/periodic interrupt and route it through the JJ12
      option to CPU NMI.
- [x] Implement all aliases caused by incompletely decoded motherboard I/O
      address bits, including DMA, CTC, PIO, both SIOs, FDC, motor, and RTC.
- [x] Represent both physical boot-ROM sockets without breaking the existing
      single-ROM Partner P and Partner G images.

## GDP card and EF9367

- [x] Drive GDP and AVDC interrupt events into the GDP PIO `ASTB`/`BSTB`
      inputs through board-level active-low conditioned signals.
- [x] Stop treating EF9367 `VB` and AVDC IRQ as GDP PIO port-A data bits.
- [x] Replace the synthetic EF9367 VBlank cadence with raster-derived timing.
- [x] Replace floating-point vector interpolation with integer Bresenham
      plotting and retain command-origin line-pattern phase.
- [x] Implement the complete standard-vector command range, including
      `18h` through `1fh` axis/diagonal shortcuts.
- [x] Make command `05h` reset both X and Y.
- [x] Implement CTRL2 character tilt and vertical/tilted orientations.
- [x] Implement light-pen commands, direct display-memory access request,
      status interrupt latches, overflow, and high-speed-write control.
- [x] Complete command `0fh` at the next `ALL`-high memory cycle, hold READY
      low through `MW`, and expose the board's active-low pixel latch on port
      `36h` D7 without disturbing AVDC `RESTRICT` on D4.
- [x] Use one-frame clear/scan timing for non-interlaced format and two-frame
      timing for interlaced format.

## SASI adapter

- [x] Implement the `10h` through `1fh` adapter aliases selected when A2/A3
      are ignored.
- [x] Expose SASI `MSG` on status bit 5 and leave unselected reads floating.
- [x] Preserve adapter REQ/DACK/DMARQ latch ordering at bus-cycle granularity.
- [x] Replace the old S1410 command shortcuts with six-byte DCBs, the ROM's
      eight-byte drive-characteristics data phase, four-byte request sense,
      separate completion status/message phases, and transaction-long BSY.

## Storage controller conformance

- [x] Compare the emulators with the Xebec S1410 owner manual and complete Z80
      example, the Intel 8272/iSBC 208 driver, the iSBX 218A programming
      cautions, both Partner ROMs, and both relevant schematics.
- [x] Decode the 8272's documented read-track, normal/deleted read/write,
      format, read-ID, and scan commands; correct N/DTL size handling,
      drive-status inputs, NDM, drive-busy, and terminal-count behavior.
- [x] Add controller-level regressions for the manufacturer sequences, SASI
      signal phases, error/sense path, 8272 multi-sector/TC behavior, EOT
      without TC, non-DMA status, seek completion, and extended commands.

## Verification gates

- [x] Add focused tests for every alias, interrupt priority combination,
      interrupt acknowledge/RETI transition, and GDP PIO strobe.
- [x] Add EF9367 tests for Bresenham tie cases, every vector command, all line
      directions/styles, character orientations, status interrupts, direct
      memory access, and both clear timings.
- [x] Add SASI status, floating-read, alias, and DMA-handshake tests.
- [x] Add official Zilog sample and real Partner ROM regression streams for
      CTC timing/reprogramming and DMA WR0-WR6 programming/ready/bus modes.
- [x] Run the complete warning-clean configure, build, and CTest suite.
- [x] Pass the real Partner P ROM floppy boot test.
- [x] Pass the real Partner G ROM floppy boot test.
- [x] Pass Partner P hard-disk CP/M boot and command execution.
- [x] Pass Partner G hard-disk CP/M boot and command execution.

## Completion record

Completed 2026-08-26. `cmake -S . -B tests/dump/build`,
`cmake --build tests/dump/build -j2`, and
`make BUILD_DIR=tests/dump/build BIN_DIR=tests/dump/bin -j2` completed without
warnings. `ctest --test-dir tests/dump/build
--output-on-failure -j4` passed all 28 tests, including CTC/DMA chip-level
regressions, both original ROM
floppy paths and both hard-disk CP/M command integrations. Temporary files are
kept in the gitignored lowercase `tests/dump` directory; no root `Testing`
directory is created.

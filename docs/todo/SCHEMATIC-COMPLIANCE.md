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
- [x] Use one-frame clear/scan timing for non-interlaced format and two-frame
      timing for interlaced format.

## SASI adapter

- [x] Implement the `10h` through `1fh` adapter aliases selected when A2/A3
      are ignored.
- [x] Expose SASI `MSG` on status bit 5 and leave unselected reads floating.
- [x] Preserve adapter REQ/DACK/DMARQ latch ordering at bus-cycle granularity.

## Verification gates

- [x] Add focused tests for every alias, interrupt priority combination,
      interrupt acknowledge/RETI transition, and GDP PIO strobe.
- [x] Add EF9367 tests for Bresenham tie cases, every vector command, all line
      directions/styles, character orientations, status interrupts, direct
      memory access, and both clear timings.
- [x] Add SASI status, floating-read, alias, and DMA-handshake tests.
- [x] Run the complete warning-clean configure, build, and CTest suite.
- [x] Pass the real Partner P ROM floppy boot test.
- [x] Pass the real Partner G ROM floppy boot test.
- [x] Pass Partner P hard-disk CP/M boot and command execution.
- [x] Pass Partner G hard-disk CP/M boot and command execution.

## Completion record

Completed 2026-08-26. `cmake -S . -B build`, `cmake --build build -j2`, and
`make -j2` completed without warnings. `ctest --test-dir build
--output-on-failure -j2` passed all 27 tests, including both original ROM
floppy paths and both hard-disk CP/M command integrations. Temporary files are
kept in the gitignored lowercase `tests/dump` directory; no root `Testing`
directory is created.

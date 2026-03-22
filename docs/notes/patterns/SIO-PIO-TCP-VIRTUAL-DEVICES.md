# Pattern: SIO/PIO Virtual Devices and TCP Redirection

Category: Z80 SIO / Z80 PIO / Virtual Devices / Networking  
Date(s): 2026-03-22

## Problem / Purpose

Capture the current emulator behavior for:

- SIO device routing and locked ports
- Serial mouse protocols (Microsoft, Mouse Systems, Logitech C7 prompt mode)
- TCP serial redirection, including modem-line control
- PIO-attached virtual devices (Covox and visual Centronics printer)

This note is intended as the implementation baseline for future refinements.

## Hardware/Port Topology in Emulator

- Main PIO: `0xD0..0xD3`
  - `0xD0`: Port A data
  - `0xD1`: Port A control
  - `0xD2`: Port B data
  - `0xD3`: Port B control
- Main SIO #1: `0xD8..0xDB`
  - `0xD8/D9`: channel A data/control
  - `0xDA/DB`: channel B data/control
- Main SIO #2: `0xE0..0xE3` (`0xE4` alias path also decoded by current port-range helper)
  - `0xE0/E1`: channel A data/control
  - `0xE2/E3`: channel B data/control

## SIO Routing and Locking

- SIO ports are represented as:
  - `sio1_a`, `sio1_b`, `sio2_a`, `sio2_b`
- `sio1_a` is locked as fixed/internal (`"Fixed internal channel"`).
- Attachable ports are:
  - `sio1_b`
  - `sio2_a`
  - `sio2_b`
- Virtual SIO devices are serviced in `service_virtual_devices()` for those three ports.

Practical effect:

- SIO1 channel A remains reserved for machine-internal behavior.
- User-configurable serial devices are attached only to free channels.

## SIO Device Types

Available device kinds:

- `none`
- `mouse_microsoft`
- `mouse_mousesystems`
- `mouse_logitech`
- `tcp_bridge`

### Modem-Line Behavior

- For locked/internal channels: `CTS=1`, `DCD=1`.
- For serial mouse devices: `CTS=1`, `DCD=1`.
- For TCP bridge:
  - `DCD` follows data-client connection by default.
  - `CTS` follows data-client connection if enabled by config.
  - Both can be overridden by control socket commands.

## Mouse Emulation

Mouse movement is injected through `inject_serial_mouse_motion(dx, dy, buttons)`.

### Microsoft Protocol

- Packet: 3 bytes.
- Framing intent: 7-bit payload style (`7N1` behavior in packet content).
- Sync bit uses bit 6 (`0x40`) in first byte.
- Left/right button bits in first byte.
- Movement split across first/second/third byte (6-bit chunks).

### Mouse Systems Protocol

- Packet: 5 bytes.
- Framing intent: `8N1`.
- First byte starts with `0x80`.
- Button semantics use active-low “button up” bits.
- Delta is split into two signed parts for X and Y.

### Logitech

Two behaviors are supported:

1. Microsoft-compatible packet mode for normal relative movement path.
2. Logitech C7 prompt-mode handling:
   - `c`/`C`: queue identification string ending with NUL.
   - `P`/`p`: queue 5-byte C7 poll report.
   - `D`/`d`: accepted as prompt-mode command (currently no-op).

C7 poll report details:

- 12-bit signed relative `dx/dy` (clamped to `[-2048, 2047]`).
- Even-parity helper sets bit 7 as parity bit.
- Button mapping includes left/middle/right in first report byte.

### SDL Relative Mouse Capture

When Logitech mouse is attached:

- Entering main window enables relative mouse mode and hides cursor.
- Leaving window disables capture and releases button state.
- `F12` toggles capture/release.
- Motion uses `xrel/yrel` with sensitivity scaling (`*1.5`) before injection.

## TCP Serial Redirection

### Sockets

Each TCP-bridge-attached SIO port uses:

- Data listener (`tcp_data_port`)
- Control listener (`tcp_control_port`)

Defaults in config struct are `6601/6602`; UI may assign per-port values when user attaches bridge.

### Data Path

- RX from data socket enters `data_rx_fifo`, then moves into serial RX queue.
- TX from emulated SIO channel is drained and forwarded to `data_tx_fifo` then socket.
- Incoming data can be gated by RTS:
  - If `tcp_require_rts` is true, remote RX-to-SIO delivery occurs only when SIO RTS is asserted.

### Control Path

Control socket accepts line commands:

- `PING` -> `PONG`
- `CTS 0|1|AUTO`
- `DCD 0|1|AUTO`

Bridge also publishes modem output changes on control socket:

- `RTS <0|1>`
- `DTR <0|1>`

### Queue/Poll Limits and Performance Notes

- SIO RX queue cap: `8192` bytes.
- TCP RX/TX queue caps: `32768` bytes each.
- Poll interval:
  - Active: `2048` ticks
  - Idle: `8192` ticks

This polling model is intentionally non-blocking and bounded, but TCP bridge still has measurable overhead compared to mouse-only virtual devices.

## PIO Virtual Devices

Available PIO attachment kinds per port A/B:

- `none`
- `covox`
- `centronics_printer` (visual)

Data path:

- On writes to `0xD0..0xD3`, only data-port writes (`D0/D2`) trigger virtual-device output handling.
- Control-port writes (`D1/D3`) do not push device payload bytes.

### Covox

- Byte output converted to normalized level: `data / 255.0`.
- Status exposed to UI as progress bar.

### Visual Centronics Printer

- Captures printable ASCII, tab, CR/LF.
- `CR` normalized to newline.
- Consecutive newline normalization applied for `LF`.
- Buffer capped (`1 MiB`), oldest half dropped when limit exceeded.

## Interrupt and Daisy-Chain Context

- System interrupt priority order (in tick loop): DMA -> CTC -> SIO1 -> SIO2 -> PIO.
- Virtual device service runs after chip ticks, preserving chip-level interrupt behavior.
- Modem inputs are applied to SIO pins each tick before `z80sio_tick()`.

## Test Coverage

Reusable tests currently present:

- `tests/test_z80sio.cpp`
- `tests/test_z80pio.cpp`

Run:

```bash
cmake -S . -B build
cmake --build build -j4
cd build && ctest --output-on-failure
```

## Known Limits / Follow-Up

- SIO emulation is significantly improved for async/control/interrupt behavior but still not full SDLC/sync/timing-complete.
- PIO emulation now covers key handshake/interrupt behavior and active-low strobe logic, but advanced edge-case timing can still be refined.
- TCP bridge is functional and controllable; further optimization may be useful for heavy traffic sessions.

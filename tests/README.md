# Unit Tests

This folder holds reusable C++ unit tests for the emulator.

## Current tests

- `test_z80sio.cpp`: Partner ROM initialization, all asynchronous frame
  timings, three-byte FIFO/error correspondence, modem latches, auto-enables,
  Wait/Ready, and all six interrupt priorities/vectors.
- `test_z80pio.cpp`: all four modes, active-low A/B handshakes, Mode 3 logic
  equations, M1 timing, and interrupt/vector flow.

## Run tests

From repository root:

```bash
make
make test
```

## Add a new unit test

1. Create a new source file in this folder using the pattern `test_<component>.cpp`.
2. Add a new executable and `add_test(...)` entry in `tests/CMakeLists.txt`.
3. Keep tests deterministic and fast so they can run on every change.

## Suggested style

- Use small focused test functions (one behavior per function).
- Print clear failure messages with file/line and condition text.
- Prefer direct chip/API calls over UI-driven test flows.

# Unit Tests

This folder holds reusable C++ unit tests for the emulator.

## Current tests

- `test_z80sio.cpp`: Z80 SIO register/control/data pin behavior, interrupt behavior, and modem/status signals.
- `test_z80pio.cpp`: Z80 PIO mode control, handshake timing, interrupt/vector flow, and bit-control logic.

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

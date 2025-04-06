![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url]

# Iskra Delta Partner Emulator

Welcome to the **Iskra Delta Partner Emulator** — a faithful, low-level recreation of the original 1980s _Iskra Delta Partner_ microcomputer.

The _Iskra Delta Partner_ was a modular Z80A-based system, developed in Slovenia (then Yugoslavia) in 1983. It featured up to 64KB of directly addressable RAM, plus a banked memory architecture with additional RAM pages and a small 2KB system EPROM. The system supported both text-only and graphical display models, optional real-time clock, and up to two floppy drives and a 10MB hard disk.

In graphical configurations (`G` models), it combined the **Signetics SCN2674 AVDC** for text mode with the **Thomson EF9367 GDP** for high-resolution raster graphics (1024×512). Both display processors had dedicated video RAM and worked together to present a unified screen.

![Iskra Delta Partner](docs/img/partner.jpg)

For deep technical details, check out:

- [Iskra Delta Partner: The Complete Reference](docs/books/PARTNER-COMPLETE-REFERENCE.md)
- [IDP-DEV: The Iskra Delta Partner Development Repository](http://github.com/tstih/idp-dev)

> This emulator targets **Linux** only and is under active development. Binary releases are not yet available.

---

## What Makes This Emulator Unique?

This emulator is **hardware-level** and **cycle-stepped** — we simulate the behavior of each chip (Z80, SIO, AVDC, GDP, 8272, etc.) down to the signal level. No shortcuts like intercepting BDOS/BIOS calls are used. All hardware timing, boot process, device initialization, and I/O occur just as on the original Partner.

It’s also a **second-generation emulator**: rather than relying solely on periodic interrupts or software timers, it carefully models the actual chip-level interactions and timing behavior.

---

## Installing and Using

## Clone

```bash
git clone https://github.com/tstih/idp-emu.git
```

## Compile

```bash
cmake -S . -B build
cmake --build build
```

## Run

...todo...

# Dependencies

...todo...

[language.url]: https://isocpp.org/
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg
[standard.url]: https://en.wikipedia.org/wiki/C%2B%2B#Standardization
[standard.badge]: https://img.shields.io/badge/C%2B%2B-20-blue.svg
[status.badge]: https://img.shields.io/badge/status-unstable-red.svg

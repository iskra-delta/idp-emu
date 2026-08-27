![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url]

# Iskra Delta Partner Emulator

Welcome to the **Iskra Delta Partner Emulator** — a faithful, low-level recreation of the original 1980s _Iskra Delta Partner_ microcomputer.

The _Iskra Delta Partner_ was a modular Z80A-based system, developed in Slovenia (then Yugoslavia) in 1983. It featured up to 64KB of directly addressable RAM, plus a banked memory architecture with additional RAM pages and a small 2KB system EPROM. The system supported both text-only and graphical display models, optional real-time clock, and up to two floppy drives and a 10MB hard disk.

In graphical configurations (`G` models), it combined the **Signetics SCN2674 AVDC** for text mode with the **Thomson EF9367 GDP** for high-resolution raster graphics (1024×512). Both display processors had dedicated video RAM and worked together to present a unified screen.

![Iskra Delta Partner](docs/img/partner.jpg)

For deep technical details, check out:

- [Iskra Delta Partner: The Complete Reference](docs/books/PARTNER-COMPLETE-REFERENCE.md)
- [IDP-DEV: The Iskra Delta Partner Development Repository](http://github.com/tstih/idp-dev)

The emulator builds and ships natively for Ubuntu Linux, macOS (Apple Silicon
and Intel), and 64-bit Windows.

---

## What Makes This Emulator Unique?

This emulator is **hardware-level** and **cycle-stepped** — we simulate the behavior of each chip (Z80, SIO, AVDC, GDP, 8272, etc.) down to the signal level. No shortcuts like intercepting BDOS/BIOS calls are used. All hardware timing, boot process, device initialization, and I/O occur just as on the original Partner.

It’s also a **second-generation emulator**: rather than relying solely on periodic interrupts or software timers, it carefully models the actual chip-level interactions and timing behavior.

---

## Installing and Using

Tagged releases provide a native Ubuntu `.deb`, macOS `.pkg`, and Windows
Setup `.exe`, plus a portable archive for each platform. Third-party runtime
libraries are linked statically where supported; the remaining non-system
libraries are carried inside the package.

## Clone

```bash
git clone https://github.com/tstih/idp-emu.git
```

## Compile

```bash
make
```

Equivalent raw CMake commands:

```bash
cmake -S . -B tests/dump/build
cmake --build tests/dump/build
cmake --install tests/dump/build --prefix "$PWD/bin"
```

## Clean

```bash
make clean
```

## Run

See the [complete command-line reference](docs/COMMAND-LINE.md) for every
option, alias, accepted value, default, escape sequence, and additional example.

Run Partner P (CRT) with floppy:

```bash
./bin/bin/idp-emu --model crt --rom roms/partner_crt.rom --disk disks/fdd-partner-p.img
```

Run Partner P (CRT) with the bootable system HDD:

```bash
./bin/bin/idp-emu --model crt --system-crt-hdd
```

Run Partner G (GDP) with HDD:

```bash
./bin/bin/idp-emu --model gdp --rom roms/partner_gdp.rom --hdd disks/hdd-partner-g.img
```

Run Partner G (GDP) with empty HDD:

```bash
./bin/bin/idp-emu --model gdp --rom roms/partner_gdp.rom --hdd disks/hdd-partner-g-empty.img
```

Run Partner G (GDP) with the basic user-area-0 CP/M system disk:

```bash
./bin/bin/idp-emu --model gdp --rom roms/partner_gdp.rom --hdd disks/hdd-partner-g-system.img
```

### Invisible MCP mode

`idp-mcp` runs the same chip-level machine without opening a window and exposes
it as a stateful Model Context Protocol server over stdin/stdout. It defaults
to the CRT model and its bundled ROM:

```bash
./bin/bin/idp-mcp
```

Start a GDP machine with a hard disk:

```bash
./bin/bin/idp-mcp --model gdp --hdd disks/hdd-partner-g-system.img
```

The server mirrors the applicable `zx-spectrum-mcp` workflow: loading, bounded
run/run-until/step control, signal breakpoints, complete registers, memory and
I/O bus access, timed keyboard input, PNG screens/screenshots, text/ASCII screen
inspection, and YUV4MPEG2 recording. `measure_cycles` reports exact elapsed
4 MHz chip clocks (Z80 T-states) for an instruction or routine. Partner media
mounting remains available; cassette control is intentionally absent because
there is no cassette chip in the Partner.
It writes JSON-RPC only to stdout; diagnostics and media-load messages go to
stderr, so it can be launched directly by an MCP client. For example:

```toml
[mcp_servers.iskra_partner]
command = "/absolute/path/to/idp-emu/bin/bin/idp-mcp"
args = ["--model", "gdp", "--hdd", "/absolute/path/to/hdd.img"]
```

See the [command-line reference](docs/COMMAND-LINE.md#invisible-mcp-server)
for startup options and the complete tool list.

In VS Code, `F5` defaults to the `Original Partner GDP CP/M` launch configuration from `.vscode/launch.json`. It boots the original Partner GDP ROM from `roms/partner_gdp.rom` with the basic CP/M hard-disk image from `disks/hdd-partner-g-system.img` and keeps its CMOS settings in `partner_cmos.bin`, separate from PartOS. The disk contains the standard CP/M Plus file and system maintenance commands (`DIR`, `PIP`, `RENAME`, `ERASE`, `TYPE`, `SHOW`, `SET`, `SETDEF`, `DATE`, `DEVICE`, and their supporting utilities) plus `PAKET.COM` in user area 0. It contains no `PROFILE.SUB`, languages, office applications, demos, or user data. The PartOS launch remains available as `PartOS Boot (Unsafe Build Only)` when needed.

### Type commands after startup

Use `--commands` to type through the emulated keyboard after the GUI opens and
the guest keyboard and display have settled. Both literal newlines and `\n`
escapes are converted to the Enter key:

```bash
./bin/bin/idp-emu --model crt --commands 'b:\ntest\n'
```

The default delay is 1000 ms before the first key and 350 ms between keys. Slow
boots can be given more time, and `--commands` can be repeated:

```bash
./bin/bin/idp-emu --commands 'b:\n' --commands 'test\n' \
  --type-delay 3000 --type-interval 350
```

Supported escapes are `\n`/`\r` (Enter), `\t` (Tab), `\b` (Backspace), `\e`
(Escape), `\\`, and `\xNN` for an exact byte.

### Covox audio and recordings

The emulator can attach a real-time 8-bit Covox DAC to either free port of the
main PIO (`D0h`–`D3h`). Port `1` is A and port `2` is B:

```bash
./bin/bin/idp-emu --model gdp --hdd disks/music.img \
  --covox-port 1 --commands 'player 1\n'
```

This does not use the GDP board's separate PIO at `30h`–`33h`. You can also
attach or detach the DAC in **Devices → PIO Devices**. When a Covox is attached
at the start of a screen recording, its 44.1 kHz mono output is included in the
AVI with the recorded image.

### Original Partner floppy compatibility

The original GDP ROM/CP/M software matrix mounts each known sibling-project
floppy as drive `B:`, launches every program, and saves a framebuffer plus a
machine-readable and Markdown report:

```bash
python3 tools/run_partner_software_matrix.py \
  --output /tmp/idp-partner-software-matrix
```

The two `*-xcc-final.img` source artifacts are empty CP/M images. Their
companion COM binaries can still be checked from temporary populated copies
(the source images are left untouched):

```bash
python3 tools/run_partner_software_matrix.py \
  --output /tmp/idp-partner-software-matrix-xcc \
  --test-xcc-binaries
```

# Portable release layout

The generated `bin/` directory is a complete copyable runtime tree:

```text
bin/
  partner_cmos.bin  initial Partner CMOS image
  bin/       idp-emu, idp-mcp, partnerp, and partnerg (Unix platforms)
  shared/    bundled dynamic libraries, when static linking is unavailable
  roms/      CRT and GDP firmware
  disks/     only the Partner P and Partner G system HDD images
  assets/    application icon, UI fonts, and other runtime data
  docs/      command-line help and release manifest
```

Default disks are copied into the platform's per-user application-data
directory before the emulator writes them. Installed seed images therefore
remain reusable. See [Packaging and releases](docs/PACKAGING.md) for build,
installer, architecture, and tagging details.

Installed Linux and macOS packages provide two convenient commands:
`partnerp` boots the CRT/P model from a writable copy of its system hard disk,
while `partnerg` boots the GDP/G model from its system hard disk. Neither
profile attaches a floppy. Windows provides `partnerp.bat` and `partnerg.bat`
with the same behavior. The installer creates matching Start Menu shortcuts,
with an optional matching pair on the desktop.

Both Partner profiles attach the built-in Retro Vault Squid service to PAKET
port 2 (SIO1B) by default on Linux, macOS, and Windows. `PAKET.COM` is included
on both model-specific system hard disks. At the CP/M
prompt, running `PAKET` therefore lists the current Partner software catalog
without an external server, proxy, serial bridge, or helper process. In
**Devices**, the same internal service can be moved to either of the other free
SIO channels; the TCP Bridge remains available as a separate attachment type.
The bundled client detects the Partner display board at runtime and uses only
plain text through the standard CP/M console on both models. It does not clear
the screen, change text attributes, draw decorative rules, or animate a
progress line. Catalog and search results use a compact aligned ID/name table.

The Windows package keeps `partner.exe`, `idp-mcp.exe`, and both batch runners
together in the application folder under Program Files. Third-party libraries
are statically linked; Windows system DLLs are loaded from the target operating
system and are never copied into the application folder. The CMOS seed is
copied from that folder into the user's application-data directory on first
launch.

## Acknowledgments

This emulator wouldn't exist without the inspiration and deep technical insight provided by the amazing [floooh/chips](https://github.com/floooh/chips) project. Huge thanks to @floooh for making such a clean and educational codebase available. Much of the structure, ideas, and even some of the wording were directly influenced by studying `chips` — thank you!

[language.url]: https://isocpp.org/
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg
[standard.url]: https://en.wikipedia.org/wiki/C%2B%2B#Standardization
[standard.badge]: https://img.shields.io/badge/C%2B%2B-20-blue.svg
[status.badge]: https://img.shields.io/badge/status-unstable-red.svg

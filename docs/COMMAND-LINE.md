# Command-Line Reference

Use the installed launch profiles for normal operation:

```bash
partnerp [options]  # Partner P/CRT system hard disk
partnerg [options]  # Partner G/GDP system hard disk
```

For lower-level configuration, run `idp-emu [options]` on Linux or macOS, or
`partner.exe [options]` on Windows. `--help` prints the complete option list.
Unknown options, missing values, invalid enumerated values, invalid ports, and
malformed command escapes stop startup with an error.

## Options

| Option | Accepted value | Default | Description |
| --- | --- | --- | --- |
| `--help` | none | — | Print every supported option and exit. |
| `--rom FILE` | ROM image path | `roms/partner_crt.rom` | Select the system ROM. |
| `--fd0 FILE` | disk-image path | Selected for the ROM and model | Attach a floppy image to drive 0. |
| `--disk FILE` | disk-image path | — | Alias for `--fd0`. |
| `--fd1 FILE` | disk-image path | no disk | Attach a floppy image to drive 1. |
| `--disk-b FILE` | disk-image path | — | Alias for `--fd1`. |
| `--hdd FILE` | disk-image path | no disk | Attach a Xebec/SASI hard-disk image. |
| `--system-floppy` | none | off | Attach a writable Partner P system floppy in source/development trees; release packages intentionally omit this image. |
| `--system-crt-hdd` | none | off | Attach a writable per-user copy of the bundled Partner P CRT system hard disk. |
| `--system-hdd` | none | off | Attach a writable per-user copy of the bundled Partner G system hard disk. |
| `--boot TYPE` | `default`, `floppy` | `default` | Use normal firmware boot selection, or automatically select floppy boot when the firmware prompts. |
| `--nvram FILE` | file path | Selected for the ROM | Choose the MM58167 shadow NVRAM backing file. |
| `--terminal TYPE` | `vt52`, `vt100`, `ansi` | VT52 for CRT; VT100 for GDP | Select terminal emulation. `ansi` is an alias for `vt100`; on GDP this also selects the BIOS ANSI terminal mode in CMOS so CSI output cannot be misread as native GDP graphics commands. |
| `--model TYPE` | `crt`, `gdp`, `auto` | `auto` | Select the text or graphics Partner model. Automatic selection uses the ROM filename. |
| `--covox-port PORT` | `1`, `2` | disabled | Attach the host-audio Covox DAC to main PIO A (`1`) or B (`2`). This is the PIO at `D0h`–`D3h`, not the GDP-board PIO at `30h`–`33h`. |
| `--sio-tcp PORT DATA CONTROL` | PAKET port `2`–`4` and two TCP ports | disabled | Attach the selected free SIO channel to an external TCP data/control bridge. |
| `--sio-squid PORT` | PAKET port `2`–`4` | Partner CRT and G: port `2` | Move the internal Squid/Retro Vault service to the selected free SIO channel. |
| `--squid-payload BYTES` | `16`–`112` | `112` | Set the internal Squid endpoint's maximum negotiated DATA payload. The peer may select a smaller value. |
| `--dap PORT` | `1`–`65535` | disabled | Start the udap Debug Adapter Protocol server on `127.0.0.1:PORT`. |
| `--commands TEXT` | escaped or literal text | no startup input | Type text through the emulated keyboard after startup. This option may be repeated. |
| `--command TEXT` | escaped or literal text | — | Alias for `--commands`. |
| `--type TEXT` | escaped or literal text | — | Alias for `--commands`. |
| `--type-delay MS` | non-negative integer | `1000` | Minimum delay in milliseconds before the first startup key. |
| `--type-interval MS` | non-negative integer | `350` | Minimum delay in milliseconds between startup keys. |
| `--type-enter-delay MS` | non-negative integer | key interval | Delay after Enter when another startup key follows. |

Relative file paths are resolved from the current directory, the executable
directory, the release-tree root, the macOS application Resources directory,
and the source directory in development builds.

## PAKET and internal Squid

Partner CRT and Partner G assign PAKET port 2 (SIO1B) to the internal
Squid/Retro Vault device by default on every platform. `PAKET.COM` is present
on the packaged Partner P and Partner G system hard disks. The device
terminates the reliable Squid serial framing inside the
emulator and performs Retro Vault HTTPS requests on a background thread. Run
`PAKET` at the CP/M `A>` prompt to list the catalog. Output is plain text
without terminal-control effects; catalog and search results use aligned
ID/name columns. During a download, `Stanje` is redrawn with the number of
bytes remaining and finishes at `preostalo: 0 bajtov`.

Open **Devices** to move Internal Squid to PAKET port 3 (SIO2A) or port 4
(SIO2B), or use `--sio-squid PORT` at startup. Selecting it on a new port
automatically detaches it from the previous one. Set `RETRO_VAULT_API_URL`
before starting the emulator to use a compatible endpoint other than the
default `https://retro-vault.org`.

The internal endpoint offers 112-byte Squid DATA frames by default. Use
`--squid-payload BYTES` to lower the offer; the negotiated value is the lower
of the emulator and PAKET settings. `PAKET -m 16` retains legacy frame sizing.

`TCP Bridge` is still a separate device choice for external serial tools. It
is not required by PAKET when Internal Squid is attached.

## Covox audio

Attach a Covox to main PIO port A and pass the corresponding port number to a
guest program:

```bash
idp-emu --model gdp --hdd disks/music.img \
  --covox-port 1 --commands 'player 1\n'
```

Port `1` means PIO A (`D0h` data, `D1h` control); port `2` means PIO B (`D2h`
data, `D3h` control). The separate GDP-board PIO at `30h`–`33h` remains
available to the graphics hardware.

Covox writes are resampled from exact emulated Z80 ticks to 44.1 kHz mono
audio. If a Covox is attached when a screen recording starts, the same samples
are stored as 16-bit PCM in the AVI alongside the 25 FPS MJPEG video. Attach
the Covox before choosing **Emulation → Start Recording**; recordings started
without one remain video-only.

## Startup commands

Startup commands begin only after the GUI has rendered. After the configured
initial delay, the first key also waits until the guest keyboard is enabled and
the terminal display has been stable for 400 ms. Later keys observe the typing
interval and wait for the previous byte to leave the emulated keyboard SIO.
This prevents early boot resets or a slow guest input routine from dropping the
first character.

The supported escapes are:

| Escape | Key or byte |
| --- | --- |
| `\n` or `\r` | Enter (`0x0D`) |
| `\t` | Tab (`0x09`) |
| `\b` | Backspace (`0x08`) |
| `\e` | Escape (`0x1B`) |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |
| `\xNN` | Exact byte represented by two hexadecimal digits |

Literal LF and CRLF characters in `TEXT` are also converted to a single Enter
key. Put command text in single quotes in a POSIX shell so that sequences such
as `\n` reach the emulator unchanged.

Repeated `--commands`, `--command`, and `--type` values are concatenated in
the order supplied. They do not add separators automatically.

For example, boot a GDP Partner and run `dir` at the CP/M prompt:

```bash
idp-emu \
  --model gdp \
  --rom roms/partner_gdp.rom \
  --hdd disks/hdd-partner-g-system.img \
  --commands 'dir\n' \
  --type-delay 3000
```

Send two command lines using repeated options:

```bash
idp-emu \
  --commands 'b:\n' \
  --commands 'test\n'
```

Override the safe 350 ms typing interval only when the guest is known to
accept input faster:

```bash
idp-emu --commands 'dir\n' --type-interval 100
```

Keep normal typing speed but allow a drive-change command to finish before the
next command starts:

```bash
idp-emu --commands 'b:\nmavrica\n' \
  --type-interval 350 --type-enter-delay 2000
```

## Machine and media examples

Partner P/CRT with a floppy in drive 0:

```bash
idp-emu \
  --model crt \
  --rom roms/partner_crt.rom \
  --fd0 disks/fdd-partner-p.img
```

Partner P/CRT with the bootable system hard disk:

```bash
idp-emu --model crt --system-crt-hdd
```

The CRT hard disk uses the same CP/M files and Xebec/SASI geometry as the
Partner G system disk, with its BIOS console routed to CRT, GDP `SETUP`
disabled, and the loader-configured CRT SIO channel retained.

Partner G/GDP with a hard disk and VT100 terminal behavior:

```bash
idp-emu \
  --model gdp \
  --rom roms/partner_gdp.rom \
  --hdd disks/hdd-partner-g.img \
  --terminal vt100
```

Attach two floppy images and start the debug server:

```bash
idp-emu \
  --fd0 disks/system.img \
  --fd1 disks/data.img \
  --dap 4711
```

## Invisible MCP server

`idp-mcp` is a headerless, stateful instance of the emulator for AI clients.
It speaks newline-delimited MCP JSON-RPC on stdin/stdout and never creates an
SDL window. Protocol output is the only content written to stdout; `--verbose`
and all emulator diagnostics use stderr.

```bash
./bin/bin/idp-mcp [options]
```

| Option | Accepted value | Default | Description |
| --- | --- | --- | --- |
| `--model` | `crt`, `gdp` | `crt` | Select the Partner board model. |
| `--rom FILE` | ROM image path | `roms/partner_MODEL.rom` | Load a ROM before serving. |
| `--no-rom` | none | disabled | Start with no ROM instead of loading the model ROM. |
| `--fd0` … `--fd3` | disk-image path | no disk | Attach a floppy to the selected drive. |
| `--hdd FILE` | hard-disk image path | no disk | Attach a Xebec/SASI image. |
| `--nvram FILE` | file path | ephemeral | Persist the MM58167 shadow bytes. With no path, no NVRAM file is written. |
| `--terminal` | `vt52`, `vt100`, `ansi` | VT52 for CRT; VT100 for GDP | Select the invisible terminal parser and matching GDP BIOS profile. |
| `--list-tools` | none | — | Print tool definitions as JSON and exit. |
| `--verbose` | none | disabled | Log JSON-RPC traffic to stderr. |
| `--version` | none | — | Print the server version and exit. |
| `--help` | none | — | Print MCP-server help and exit. |

The MCP tools are:

| Tool | Purpose |
| --- | --- |
| `load` | Load inline/file binary data or a 2K Partner ROM, or attach `fd0`–`fd3`/`hdd`. |
| `status` | Inspect CPU, bus, DMA, FDC, banking, model, cumulative cycles, and timing metadata. |
| `reset` | Reset the complete chip set while retaining RAM and media; `clear_memory` also wipes both RAM banks. |
| `run` | Run by nominal 60 Hz frames, T-states/ticks, instructions, or legacy `until_pc`; breakpoints always stop it. |
| `run_until` | Run to an instruction-boundary address with a `max_tstates` guard. |
| `step` | Execute complete instructions and return exact elapsed clocks. |
| `measure_cycles` | Measure exact elapsed 4 MHz chip clocks/T-states for instructions or a routine ending at `until_pc`. |
| `registers` | Read or update the main/shadow Z80 registers, interrupt state, and PC. |
| `read_memory` / `write_memory` | Access wrapping CPU-visible memory using byte arrays or hex text; writes can explicitly patch visible ROM. |
| `breakpoint` | Manage execute, memory-read/write, and I/O-read/write signal breakpoints with optional data matching. |
| `read_port` / `set_port` | Perform actual motherboard I/O accesses without running the CPU. |
| `read_io` / `write_io` | Backward-compatible aliases of `read_port` / `set_port`. |
| `press_keys` | Type text or named keys through the physical keyboard SIO while advancing emulated time. |
| `keyboard` | Queue raw bytes through the keyboard SIO, optionally spacing them by exact ticks. |
| `screen` | Return the current CRT/GDP chip raster as an MCP PNG image. |
| `screen_text` | Read terminal characters and serial/printer transcripts, or return framebuffer ASCII art. |
| `screenshot` | Save the current chip raster as PNG. |
| `video_start` / `video_stop` | Record raster frames during emulation to a nominal-60-Hz YUV4MPEG2 file. |
| `mount_media` | Attach `fd0`–`fd3` or `hdd` while the server is running. |

The names and argument shapes follow the applicable commands in
`zx-spectrum-mcp`. Spectrum-only snapshots, screen dumps, and cassette
transport are not exposed: Partner has different display/media chips and no
cassette interface. Partner-specific raw keyboard and media tools remain for
low-level work.

Every run request is bounded. With no limit, `run` advances one nominal 60 Hz
slice (66,667 clocks). `max_ticks`/`max_tstates` protect instruction and address
runs from a wedged guest. One `tick`, `tstate`, or `cycle` in MCP results is one
call through the full chip set at the emulator's 4 MHz CPU/master clock; DMA
bus ownership and chip wait behavior are therefore included in elapsed cycle
measurements. Numeric inputs also accept decimal numbers and strings beginning
with `0x`, `$`, or `#`.

For example, load a NOP at `8000h` and measure it:

```json
{"name":"load","arguments":{"data":"00","address":"0x8000","start":"0x8000"}}
{"name":"measure_cycles","arguments":{"instructions":1}}
```

The measurement reports four cycles/T-states and ends at `8001h`.

Example MCP client configuration:

```toml
[mcp_servers.iskra_partner]
command = "/home/user/idp-emu/bin/bin/idp-mcp"
args = ["--model", "gdp", "--hdd", "/home/user/disks/partner.img"]
```

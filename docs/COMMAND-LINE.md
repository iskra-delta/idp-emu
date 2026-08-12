# Command-Line Reference

Run the emulator as:

```bash
./bin/idp-emu [options]
```

`./bin/idp-emu --help` prints the same complete option list. Unknown options,
missing values, invalid enumerated values, invalid ports, and malformed command
escapes stop startup with an error.

## Options

| Option | Accepted value | Default | Description |
| --- | --- | --- | --- |
| `--help` | none | — | Print every supported option and exit. |
| `--rom FILE` | ROM image path | PartOS ROM when available; otherwise `roms/partner_crt.rom` | Select the system ROM. |
| `--fd0 FILE` | disk-image path | Selected for the ROM and model | Attach a floppy image to drive 0. |
| `--disk FILE` | disk-image path | — | Alias for `--fd0`. |
| `--fd1 FILE` | disk-image path | no disk | Attach a floppy image to drive 1. |
| `--disk-b FILE` | disk-image path | — | Alias for `--fd1`. |
| `--hdd FILE` | disk-image path | PartOS HDD for the default PartOS ROM; otherwise no disk | Attach a Xebec/SASI hard-disk image. |
| `--boot TYPE` | `default`, `floppy` | `default` | Use normal firmware boot selection, or automatically select floppy boot when the firmware prompts. |
| `--nvram FILE` | file path | Selected for the ROM | Choose the MM58167 shadow NVRAM backing file. |
| `--terminal TYPE` | `vt52`, `vt100`, `ansi` | VT52 for CRT; VT100 for GDP | Select terminal emulation. `ansi` is an alias for `vt100`. |
| `--model TYPE` | `crt`, `gdp`, `auto` | `auto` | Select the text or graphics Partner model. Automatic selection uses the ROM filename. |
| `--covox-port PORT` | `1`, `2` | disabled | Attach the host-audio Covox DAC to main PIO A (`1`) or B (`2`). This is the PIO at `D0h`–`D3h`, not the GDP-board PIO at `30h`–`33h`. |
| `--dap PORT` | `1`–`65535` | disabled | Start the udap Debug Adapter Protocol server on `127.0.0.1:PORT`. |
| `--commands TEXT` | escaped or literal text | no startup input | Type text through the emulated keyboard after startup. This option may be repeated. |
| `--command TEXT` | escaped or literal text | — | Alias for `--commands`. |
| `--type TEXT` | escaped or literal text | — | Alias for `--commands`. |
| `--type-delay MS` | non-negative integer | `1000` | Minimum delay in milliseconds before the first startup key. |
| `--type-interval MS` | non-negative integer | `350` | Minimum delay in milliseconds between startup keys. |
| `--type-enter-delay MS` | non-negative integer | key interval | Delay after Enter when another startup key follows. |

Relative file paths are resolved from the current directory, the executable
directory, and the project directory when the executable is in `bin/`.

## Covox audio

Attach a Covox to main PIO port A and pass the corresponding port number to a
guest program:

```bash
./bin/idp-emu --model gdp --hdd disks/music.img \
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
./bin/idp-emu \
  --model gdp \
  --rom roms/partner_gdp.rom \
  --hdd disks/hdd-partner-g-system.img \
  --commands 'dir\n' \
  --type-delay 3000
```

Send two command lines using repeated options:

```bash
./bin/idp-emu \
  --commands 'b:\n' \
  --commands 'test\n'
```

Override the safe 350 ms typing interval only when the guest is known to
accept input faster:

```bash
./bin/idp-emu --commands 'dir\n' --type-interval 100
```

Keep normal typing speed but allow a drive-change command to finish before the
next command starts:

```bash
./bin/idp-emu --commands 'b:\nmavrica\n' \
  --type-interval 350 --type-enter-delay 2000
```

## Machine and media examples

Partner P/CRT with a floppy in drive 0:

```bash
./bin/idp-emu \
  --model crt \
  --rom roms/partner_crt.rom \
  --fd0 disks/fdd-partner-p.img
```

Partner G/GDP with a hard disk and VT100 terminal behavior:

```bash
./bin/idp-emu \
  --model gdp \
  --rom roms/partner_gdp.rom \
  --hdd disks/hdd-partner-g.img \
  --terminal vt100
```

Attach two floppy images and start the debug server:

```bash
./bin/idp-emu \
  --fd0 disks/system.img \
  --fd1 disks/data.img \
  --dap 4711
```

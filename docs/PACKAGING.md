# Packaging and Releases

## Copyable runtime tree

`make` builds and stages a relocatable tree under `bin/`; `make test` also runs
the full test suite:

```text
bin/
  partner_cmos.bin  Initial eight-byte Partner CMOS image (ANSI terminal)
  bin/       idp-emu[.exe], idp-mcp[.exe], partnerp, partnerg (Unix)
  shared/    bundled non-system dynamic libraries, if any
  assets/    fonts, Partner/MCP component icons, and UI data
  roms/      original CRT and GDP 2 KiB ROMs
  disks/     P and G system hard-disk images only
  docs/      usage and release metadata
```

Copy the outer `bin/` directory to another machine with a compatible OS and
architecture. The executables find resources relative to themselves, not the
checkout or current directory. Default writable media and settings are placed
in the user's platform application-data directory. Every platform retains the
original multi-resolution files as `assets/icons/partner.ico` and
`assets/icons/mcp.ico`, in addition to its native icon representation.

Windows uses a deliberately flatter version of the same tree. `partner.exe`,
`idp-mcp.exe`, `partnerp.bat`, and `partnerg.bat` live at the
root copied to `Program Files\Iskra Delta Partner Emulator`; `roms`, `disks`,
`assets`, and `docs` remain resource subdirectories. Third-party libraries and
the MSVC runtime are linked statically. Windows system DLLs are never packaged,
because local copies would override the target machine's compatible versions.
`partner_cmos.bin` is installed at the root and copied to the user's
application-data directory on first launch. `partnerp` boots the CRT/P model
with `hdd-partner-p-system.img`; `partnerg` boots the GDP/G model with
`hdd-partner-g-system.img`. Both launchers attach only their hard disk and no
floppy. The installer creates matching
Start Menu shortcuts, and its optional desktop-shortcut task creates the same
pair.

System hard disks are copied to the per-user data directory because CP/M must
be able to write to them. Each copy carries a fingerprint of its packaged seed.
When an upgraded package contains a changed seed, the emulator activates that
seed and preserves the old writable image beside it with a `.previous` suffix.
Guest changes remain untouched while the packaged seed is unchanged. Packaged
resources also take precedence over same-named files in the launch directory,
preventing an installed executable from mixing files from a source checkout.

The packaged CMOS seed selects the GDP BIOS ANSI terminal mode and carries a
valid NVRAM checksum. Recreate it after changing its defaults with
`python3 tools/make_partner_cmos.py`; package verification checks the exact
eight-byte result. The GDP emulator does not override the terminal mode:
Partner CP/M reads and implements the selection stored in CMOS.

All three platform packages contain exactly two disk images:
`hdd-partner-p-system.img` and `hdd-partner-g-system.img`. Packaged media
verification rejects any floppy, empty, application, or other disk image and
checks that the same current `PAKET.COM` is present on both system hard disks.
Both hard disks contain exactly the standard CP/M Plus maintenance
set—including `PIP`, `RENAME`, `ERASE`, `DIR`, `TYPE`, `SHOW`, and `SETDEF`—
plus PAKET, with no applications or user data. The Squid-over-SIO integration
suite cold-boots each Partner model, opens the internal Squid serial link, and
retrieves live catalog and download data. `PAKET.COM` is the guest client used
to drive that protocol test; it is not the integration or server itself.

When replacing `PAKET.COM` on either hard disk, use
`tools/update_partner_hdd_file.py` instead of removing and re-adding it with
an older `cpmdisk`. Partner hard disks use `EXM=1`, so one directory entry can
describe two 16 KiB logical extents. The updater preserves that packed layout
and its allocation; a corrected `cpmdisk` is required when a larger payload
needs new blocks. Release verification rejects overlapping `EX=0` and `EX=1`
entries before a broken image can be packaged.

The Ubuntu package installs `partnerp` and `partnerg` commands in `/usr/bin`
and matching desktop entries. The macOS package installs the same command
names in `/usr/local/bin`, plus Partner P and Partner G app launchers. Both use
the writable system-media profiles described above. The macOS MCP component
has its own app and icon as well. All three macOS apps delegate to the single
resource-bearing Partner P bundle, so libraries and disk images are
not duplicated. Windows provides the two equivalent Start Menu and optional
desktop shortcuts. Package construction validates the batch launchers, icons,
and byte-for-byte CMOS seed before producing an installer. Upgrades remove the
former `partner` command and legacy Partner/Classic/Graphical shortcuts.

Every native build compiles the platform-neutral Retro Vault request and JSON
core from a pinned `retro-plastics/squid-server` revision and the exact
portable wire-protocol engine from a pinned `retro-plastics/libsquid` revision
directly into the emulator. The wrapper only connects emulated SIO bytes to
the library and preserves channel-3 packet boundaries. HTTPS uses libcurl.
No `squid-server` executable, plugin, `socat` bridge,
system service, configuration file, or public listening socket is packaged.
The same implementation is therefore present in Ubuntu, macOS, and Windows
installers. Dynamic libcurl dependencies, when needed, use the normal
relocatable `shared/` directory; the Windows release links them statically.
`PAKET.COM` is embedded in both packaged CP/M system hard disks, and both
Partner model profiles attach Internal Squid to SIO1B by default. The
same client binary detects the display board at runtime and uses only plain
text through the standard CP/M console on both models.

To stage a clean release tree explicitly:

```bash
cmake -S . -B tests/dump/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tests/dump/build
ctest --test-dir tests/dump/build --output-on-failure
cmake --install tests/dump/build --prefix "$PWD/bin"
python3 scripts/package/verify_bundle.py \
  --root bin --platform linux \
  --version "$(tr -d '\r\n' < tests/dump/build/idp-version.txt)"
```

## Tagged release pipeline

Pushing a strict semantic release tag such as `v1.2.3` starts
`.github/workflows/release.yml`. The workflow builds and runs the full test
suite independently on each native runner. The exact tag is the canonical
version source for CMake, binaries, installers, archives, and package metadata.
CMake strips the leading `v` and rejects an explicit `IDP_VERSION` that
conflicts with the checked-out tag. Untagged source trees retain the development
fallback; source archives without Git metadata can pass
`-DIDP_VERSION=X.Y.Z` explicitly.

After verifying relocation and embedded MCP commands, the workflow publishes
these GitHub release assets:

- Ubuntu x86_64 `.deb` and portable `.tar.gz`
- macOS arm64 and x86_64 `.pkg` installers and portable `.tar.gz` archives
- Windows x86_64 Inno Setup `.exe` and portable `.zip`

The release uses pinned udap, vcpkg, ImGui, and JSON dependencies. vcpkg's
static triplets and static compiler runtimes are used where the platform
supports them. Kernel/OS interfaces remain native: glibc and OpenGL on Ubuntu,
Apple system frameworks on macOS, and Windows system DLLs. The staging check
fails when another non-system runtime library escapes the release tree.

The generated installers are unsigned unless the workflow is later supplied
with platform code-signing identities. They remain installable, but macOS
Gatekeeper and Windows SmartScreen can display an unknown-publisher warning.

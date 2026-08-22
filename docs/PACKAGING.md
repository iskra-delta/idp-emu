# Packaging and Releases

## Copyable runtime tree

`make` builds and stages a relocatable tree under `bin/`; `make test` also runs
the full test suite:

```text
bin/
  partner_cmos.bin  Initial eight-byte Partner CMOS image
  bin/       idp-emu[.exe], idp-mcp[.exe]
  shared/    bundled non-system dynamic libraries, if any
  assets/    fonts, application icon, and UI data
  roms/      original CRT and GDP 2 KiB ROMs
  disks/     floppy images and initial/system hard-disk images
  docs/      usage and release metadata
```

Copy the outer `bin/` directory to another machine with a compatible OS and
architecture. The executables find resources relative to themselves, not the
checkout or current directory. Default writable media and settings are placed
in the user's platform application-data directory.

Windows uses a deliberately flatter version of the same tree. `partner.exe`,
`idp-mcp.exe`, and any non-system DLLs all live at the root copied to
`Program Files\Iskra Delta Partner Emulator`; `roms`, `disks`, `assets`, and
`docs` remain resource subdirectories. `partner_cmos.bin` is installed at the
root and copied to the user's application-data directory on first launch.

The Ubuntu package registers the icon in the freedesktop `hicolor` theme and
references it by name from the desktop entry. The macOS package compiles the
same artwork into `partner.icns`, places it in the app's Resources directory,
and declares it through `CFBundleIconFile`. Package construction validates the
launchers, icon, and byte-for-byte CMOS seed before producing an installer.

To stage a clean release tree explicitly:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/bin"
python3 scripts/package/verify_bundle.py \
  --root bin --platform linux --version 1.1.0
```

## Tagged release pipeline

Pushing a strict semantic release tag such as `v1.2.3` starts
`.github/workflows/release.yml`. The workflow builds and runs the full test
suite independently on each native runner, verifies relocation and embedded
MCP commands, and publishes these GitHub release assets:

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

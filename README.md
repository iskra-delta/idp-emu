![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url] 

# Iskra Delta Partner Emulator

Welcome to the **Iskra Delta Partner Emulator**. 

*Iskra Delta Partner* was a computer developed by the *Iskra Delta* company, in Slovenia (former Yugoslavia), behind the Iron curtain in 1983. It featured a *Z80* processor with 128KB of (banked) RAM and 4KB of ROM. In addition, it had a built-in 12" (monochrome) monitor supporting a 132x26 text mode and optional 1024x512 high-resolution graphics. The basic model came with a 5.25" floppy disk and an optional 10MB hard disk. The operating system was *CP/M* 3.0.

![Iskra Delta Partner](docs/img/partner.jpg)

More technical details about *Iskra Delta Partner* can be found here:
 * [Iskra Delta Partner: The Complete Reference](docs/PARTNER.md)
 * [IDP-DEV: The Iskra Delta Partner Development Repository](http://github.com/tstih/idp-dev)

This emulator is available only for Linux. Navigate to the [Installing and Using](#installing-and-using) chapter for downloads. 

We emulate hardware directly, i.e., there are no interceptions of BIOS or BDOS calls, common to *CP/M* emulators. Furthermore, the emulator is second-generation. Therefore, it strives to be cycle-accurate instead of just depending on the primary interrupt (such as the screen blanking) for timed emulation tasks, typical for first-generation emulators. 

# Installing and Using

The emulator is in the early stages of development, and no deployment packages are available. Therefore, you can only compile it from the source code. 

## Clone

~~~
git clone https://github.com/tstih/idp-emu.git --recursive
~~~

## Compile

~~~
cmake -S . -B build
cmake --build build
~~~

The executable is `build/partner`. Put it and the `partner.rom` into 
the same folder. 

## Run

~~~
src/partner -r partner.rom
~~~

# Dependencies

The emulator uses OpenGL and depends on the following libraries:
 * [cxx_argp argument parser](https://github.com/pboettch/cxx_argp)
 * [glfw library for OpenGL](https://github.com/glfw/glfw)
 * [imgui user interface library](https://github.com/ocornut/imgui)

[language.url]:   https://isocpp.org/
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg

[standard.url]:   https://en.wikipedia.org/wiki/C%2B%2B#Standardization
[standard.badge]: https://img.shields.io/badge/C%2B%2B-20-blue.svg

[status.badge]:  https://img.shields.io/badge/status-unstable-red.svg

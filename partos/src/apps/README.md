# apps

This directory holds user-space PartOS applications.

Applications live outside the kernel, driver and OS-layer source trees and are
meant to be built as separate loadable `.COM` images. Each `.COM` wraps one
relocatable `.XL` payload plus a tiny metadata header (stack size, embedded
XL span, entry hint).

Current applications:

- `partos/src/apps/shell/`
- `partos/src/apps/ls/`
- `partos/src/apps/ps/`
- `partos/src/apps/mem/`
- `partos/src/apps/cat/`
- `partos/src/apps/cd/`
- `partos/src/apps/mkdir/`
- `partos/src/apps/rmdir/`
- `partos/src/apps/del/`
- `partos/src/apps/cp/`
- `partos/src/apps/mv/`
- `partos/src/apps/clear/`
- `partos/src/apps/echo/`
- `partos/src/apps/help/`

Build integration lives in `partos/Makefile`, and `tools/mkdosdisk.py` packs
the resulting `.COM` images into the boot disk. The current shell launches
commands in the foreground: it waits for the child process to exit before it
prints the next prompt.

# os

This directory now holds the higher-level PartOS layer that starts after the
micro-kernel has installed page 0, brought the scheduler online, and jumped to
`0xC000`.

The live pieces here are:

- `entry.s`: first OS thread entry; caches the machine model, snapshots NVRAM,
  installs `rst 0x10`, wires the scheduler tick, initializes drivers, and
  registers the public `"partos"` service.
- `boot.s`: boot-volume bootstrap; mounts the ROM-selected FAT volume, loads
  `/SHELL.COM`, starts it as a normal process, and later resolves `/NAME.COM`
  launches for the shell.
- `console.s`: public console/keyboard bridge used by userland commands.
- `process.s`: process objects, COM/XL loader, relocator, and process reaping.
- `service.s`, `syscall.s`, `sysinfo.s`, `timer.s`: named services, exported
  syscall table, shared system snapshot, and soft timers.
- `fat*.s`: async FAT12/FAT16 mount, lookup, open/read/write, create, readdir,
  and mutation support on top of the block-device layer.

The current user-visible OS stack is no longer hypothetical:

- the boot volume is FAT-backed,
- the shell is a relocatable `.COM` program,
- the current command set includes `ls`, `ps`, `mem`, `cat`, `cp`, `mv`,
  `del`/`rm`, `mkdir`/`rmdir`, `touch`, `clear`, `echo`, and `help`,
- commands are loaded into heap-backed buffers, relocated in place, started as
  processes with their own stacks, and cleaned up on exit.

The authoritative design notes live in:

- `partos/docs/PARTOS-VOLUME-3-OS.md`

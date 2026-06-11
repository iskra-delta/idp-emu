# Device Drivers

## Concepts

partos separates two things that are easy to confuse:

- A **driver** (`dev_drv_s`) is a type of hardware — one floppy driver covers all floppy drives.
- A **device instance** (`dev_s`) is a specific unit that probe discovered — drive `A:` and drive `B:` are two instances of the same driver.

There is always one driver per hardware type and zero or more instances per driver.

---

## Structures

### `dev_drv_s` — driver descriptor (10 bytes)

Declared statically, usually in `.area _CODE`. Each hardware driver defines exactly one of these.

```
offset  size  field
0       2     open      ; open function pointer
2       2     close     ; close function pointer
4       2     read      ; read function pointer
6       2     write     ; write function pointer
8       2     ioctl     ; ioctl function pointer
```

Assembly declaration (from `tty.s`):

```asm
tty_dev_drv::
            .dw     _tty_open
            .dw     _tty_close
            .dw     _tty_read
            .dw     _tty_write
            .dw     _tty_ioctl
```

### `dev_s` — device instance (20 bytes)

Allocated at boot inside the device table. Each discovered unit gets one slot.

```
offset  size  field
0       8     name      zero-terminated instance name ("A:", "B:", "HD0:")
8       1     reserved  pads name to 9 bytes for safe null termination
9       1     flags     DEV_F_* status bits
10      8     data      driver-private per-instance state (port, unit#, etc.)
18      2     driver    pointer to the owning dev_drv_s
```

The `data` field is 8 bytes of opaque storage owned by the driver. A floppy driver stores the drive number here; a UART driver might store the base port address and baud rate. The kernel never inspects it.

---

## Driver Table

The driver table is a static, NULL-terminated list of probe function addresses in `.area _DRIVERS`. Every driver in the system registers here at link time.

```asm
driver_table::
            .dw     i8272_probe
            .dw     xebec1410_probe
            .dw     tty_probe
            .dw     0x0000          ; sentinel
```

No dynamic registration. Drivers not linked in are not present.

---

## Probe Convention

`probe` is not part of `dev_drv_s`. It is a standalone function called once at boot by the device manager, never again.

```
;; <hl> *next_free <= probe(<hl> *next_free)
;;
;; input(s):
;;  hl  ... pointer to the next free dev_s slot in the device table
;; output(s):
;;  hl  ... advanced past any dev_s entries this driver filled in
;;  f   ... Z if at least one instance was registered, NZ if none
;; destroys:
;;  a, bc, de
```

The driver fills in as many `dev_s` slots as it finds hardware for, then returns `hl` pointing to the slot after the last one it wrote. If it finds no hardware it must not advance `hl` and must return NZ.

The caller computes `(new_hl - old_hl) / DEV_SIZE` to know how many instances were added.

Example — a floppy driver that detects two drives:

```asm
i8272_probe::
            ;; ... detect drives ...
            ;; fill slot 0: "A:"
            ld      (hl),#'A'
            ;; ... fill remaining dev_s fields ...
            ld      bc,#DEV_SIZE
            add     hl,bc           ; advance to next slot
            ;; fill slot 1: "B:"
            ;; ... fill dev_s fields ...
            add     hl,bc           ; advance past slot 1
            or      a               ; Z: found something
            ret
```

---

## Boot Sequence

The device manager runs once during kernel init:

1. Zero the entire device table (`DEV_MAX × DEV_SIZE` bytes).
2. Point `hl` at the start of the device table.
3. Walk `driver_table[]`; for each non-NULL entry call `probe(hl)`.
4. Update `hl` with the returned value (next free slot).
5. Stop early if the table is full (`hl >= table_end`).

After boot the device table is fixed. No hot-plug.

---

## Writing a Driver

A minimal driver has four parts:

**1. Include the definitions**

```asm
            .include "dev.inc"
```

**2. Private functions** (prefixed `_drivername_`, not exported)

```asm
_mydrv_open:
            ret

_mydrv_close:
            ret

_mydrv_read:
            ret

_mydrv_write:
            ret

_mydrv_ioctl:
            ret
```

**3. Driver descriptor** (exported, named `drivername_dev_drv`)

```asm
mydrv_dev_drv::
            .dw     _mydrv_open
            .dw     _mydrv_close
            .dw     _mydrv_read
            .dw     _mydrv_write
            .dw     _mydrv_ioctl
```

**4. Probe function** (exported, named `drivername_probe`)

```asm
mydrv_probe::
            ;; detect hardware
            ;; if not present:
            ;;   cp #0xff        ; NZ
            ;;   ret
            ;; fill dev_s at (hl), advance hl by DEV_SIZE
            or      a           ; Z = found
            ret
```

Register the probe in `driver_table[]`.

---

## Calling a Driver Function

To call, for example, `write` on a device whose `dev_s` pointer is in `hl`:

```asm
            ld      de,hl                   ; save dev_s pointer
            ld      hl,#DEV_DRIVER
            add     hl,de                   ; hl -> dev_s.driver field
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                     ; hl = dev_drv_s pointer
            ld      bc,#DRV_WRITE
            add     hl,bc                   ; hl -> dev_drv_s.write field
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                     ; hl = write function address
            jp      (hl)
```

---

## Constants

Flags and IOCTL commands are defined in `include/dev.h` (C) and mirrored in `include/dev.inc` (assembly).

| Constant | Value | Meaning |
|----------|-------|---------|
| `DEV_MAX` | 16 | maximum device instances |
| `DEV_F_OPEN` | 0x01 | instance is open |
| `DEV_F_BUSY` | 0x02 | operation in progress |
| `DEV_F_ERROR` | 0x04 | last operation failed |
| `DEV_F_LOCKED` | 0x10 | locked against concurrent access |
| `IOCTL_GETFLAGS` | 0x01 | read flags into `de` |
| `IOCTL_SETFLAGS` | 0x02 | set flags from `de` |
| `IOCTL_CLRFLAGS` | 0x03 | clear flags from `de` |

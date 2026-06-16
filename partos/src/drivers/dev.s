            ;; dev.s
            ;;
            ;; device list management for the kernel-owned driver set.
            ;; probe results are appended to the single global device list.
            ;;
            ;; 2026-06-14   tstih
            .module dev

            .include "dev.inc"

            .globl  _list_append
            .globl  nvram_probe
            .globl  rtc_probe
            .globl  gdp_probe
            .globl  sio_probe
            .globl  fd_probe
            .globl  hd_probe

            .globl  _dev_init
            .globl  _dev_probe_all
            .globl  _find_dev_drv

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; _dev_init()
            ;; ----------------------------------------------------------------
            ;; clears the global device list. call once before the driver
            ;; probe pass.
            ;; ----------------------------------------------------------------
_dev_init::
            ld      hl,#0x0000
            ld      (dev_list),hl
            ret

            ;; ----------------------------------------------------------------
            ;; _dev_probe_all()
            ;; ----------------------------------------------------------------
            ;; runs the built-in kernel driver probes and appends every
            ;; returned chain to the single global device list.
            ;; ----------------------------------------------------------------
_dev_probe_all::
            call    sio_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append

            call    rtc_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append

            call    nvram_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append

            call    gdp_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append

            call    fd_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append

            call    hd_probe
            ex      de,hl
            ld      hl,#dev_list
            call    _list_append
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> *drv, <de> *dev <= __find_dev_drv$(<hl> *name)
            ;; ----------------------------------------------------------------
            ;; finds a device by name in the global device list and returns
            ;; its driver through the dev_t back pointer.
            ;; ----------------------------------------------------------------
__find_dev_drv$:
            ld      de,(dev_list)
fdd_loop$:
            ld      a,d
            or      e
            jr      z,fdd_fail$
            push    hl
            push    de
            inc     de
            inc     de
fdd_cmp$:
            ld      a,(de)
            cp      (hl)
            jr      nz,fdd_next$
            or      a
            jr      z,fdd_found$
            inc     de
            inc     hl
            jr      fdd_cmp$
fdd_next$:
            pop     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     hl
            jr      fdd_loop$
fdd_found$:
            pop     de
            pop     hl
            ld      hl,#DEV_DRIVER
            add     hl,de
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            xor     a
            ret
fdd_fail$:
            ld      hl,#0x0000
            ld      de,#0x0000
            ld      a,#0xff
            or      a
            ret

_find_dev_drv::
            call    __find_dev_drv$
            ex      de,hl
            ret

            .area   _INITIALIZED
dev_list::
            .dw     0x0000

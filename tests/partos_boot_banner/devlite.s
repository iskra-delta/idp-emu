            ;; devlite.s
            ;;
            ;; minimal device list support for the boot banner harness
            ;;
            ;; 2026-06-13   tstih
            .module partos_boot_banner_dev

            .include "../../partos/src/bios/core/dev.inc"

            .globl  append_list
            .globl  sio_probe
            .globl  gdp_probe

            .area   _CODE

dev_init::
            ld      hl,#0x0000
            ld      (dev_list),hl
            ret

dev_probe_all::
            call    sio_probe
            ex      de,hl
            ld      hl,#dev_list
            call    append_list

            call    gdp_probe
            ex      de,hl
            ld      hl,#dev_list
            call    append_list
            ret

find_dev_drv::
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

            .area   _SYSVARS
dev_list::
            .ds     2

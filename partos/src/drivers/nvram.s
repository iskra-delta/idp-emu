            ;; nvram.s
            ;;
            ;; partner mm58167 nvram bios driver
            ;;
            ;; protocol is intentionally simple to keep rom usage low:
            ;;   nvram.read/write always transfer exactly 8 raw bytes at a8-af
            ;;
            ;; 2026-06-13   tstih
            .module nvram

            .include "drv.inc"
            .include "nvram.inc"

            .globl  drv_open_ok
            .globl  drv_close_nop
            .globl  drv_ioctl_unsupported

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *dev <= nvram_probe()
            ;; ----------------------------------------------------------------
            ;; mm58167 nvram is mandatory on partner, so probe does not touch
            ;; hardware at all. it simply returns the one static nvram device.
            ;;
            ;; output(s):
            ;;  hl  ... nvram device
            ;; destroys:
            ;;  hl
            ;; ----------------------------------------------------------------
nvram_probe::
            ld      hl,#nvram_dev$
            ret

nvram_check_len$:
            ld      a,b
            or      a
            jr      z,nvram_check_len_low$
            ld      hl,#DRV_ERR
            ret
nvram_check_len_low$:
            ld      a,#NVRAM_SIZE
            cp      c
            ret     z
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= nvram_read(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
nvram_read::
            call    nvram_check_len$
            ret     nz
            ld      c,#NVRAM_PORT_BASE
            ld      b,#NVRAM_SIZE
nvram_read_loop$:
            in      a,(c)
            ld      (de),a
            inc     de
            inc     c
            djnz    nvram_read_loop$
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= nvram_write(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
nvram_write::
            call    nvram_check_len$
            ret     nz
            ld      c,#NVRAM_PORT_BASE
            ld      b,#NVRAM_SIZE
nvram_write_loop$:
            ld      a,(de)
            out     (c),a
            inc     de
            inc     c
            djnz    nvram_write_loop$
            ld      hl,#DRV_OK
            ret

nvram_dev_drv::
            .dw     0x0000
            .dw     nvram_probe
            .dw     drv_open_ok
            .dw     drv_close_nop
            .dw     nvram_read
            .dw     nvram_write
            .dw     drv_ioctl_unsupported

nvram_dev$:
            .dw     0x0000
            .db     'n','v','r','a','m',0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     nvram_dev_drv

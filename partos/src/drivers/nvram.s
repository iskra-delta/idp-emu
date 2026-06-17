            ;; nvram.s
            ;;
            ;; partner mm58167 nvram bios driver
            ;;
            ;; protocol is intentionally simple to keep rom usage low:
            ;;   nvram.read/write always transfer exactly 8 raw bytes at a8-af
            ;;
            ;; 2026-06-13   tstih
            .module nvram

            .include "dev.inc"
            .include "drv.inc"
            .include "nvram.inc"

            .globl  drv_open_ok
            .globl  drv_close_nop
            .globl  drv_ioctl_unsupported
            .globl  drv_signal_done
            .globl  nvram_init
            .globl  nvram_dev
            .globl  avdc_dev_drv

            .area   _CODE

            ;; ----------------------------------------------------------------
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
            call    drv_signal_done     ; ix = event (immediate completion)
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
            call    drv_signal_done     ; ix = event (immediate completion)
            ld      hl,#DRV_OK
            ret

            ;; driver-level init: the mm58167 needs none.
nvram_init::
            ld      hl,#DRV_OK
            ret

nvram_dev_drv::
            .dw     avdc_dev_drv
            .dw     0x0000
            .dw     nvram_init
            .dw     drv_open_ok
            .dw     drv_close_nop
            .dw     nvram_read
            .dw     nvram_write
            .dw     drv_ioctl_unsupported

nvram_dev::
nvram_dev$:
            .dw     0x0000
            .db     'n','v','r','a','m',0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     nvram_dev_drv

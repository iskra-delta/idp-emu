            ;; rtc.s
            ;;
            ;; partner mm58167 rtc bios driver
            ;;
            ;; protocol is intentionally simple to keep rom usage low:
            ;;   rtc.read/write always transfer exactly 6 bytes:
            ;;     sec, min, hour, mday, mon, year
            ;;
            ;; 2026-06-13   tstih
            .module rtc

            .include "dev.inc"
            .include "drv.inc"
            .include "rtc.inc"

            .globl  bcd_to_bin
            .globl  bin_to_bcd
            .globl  drv_open_ok
            .globl  drv_close_nop
            .globl  drv_ioctl_unsupported
            .globl  drv_signal_done
            .globl  rtc_init
            .globl  rtc_dev
            .globl  nvram_dev_drv

            .area   _CODE

            ;; ----------------------------------------------------------------
rtc_check_time_len$:
            ld      a,b
            or      a
            jr      z,rtc_check_time_len_low$
            ld      hl,#DRV_ERR
            ret
rtc_check_time_len_low$:
            ld      a,#RTC_TIME_SIZE
            cp      c
            ret     z
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= rtc_read(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
rtc_read::
            call    rtc_check_time_len$
            ret     nz
            ld      hl,#rtc_time_ports$
            ld      b,#RTC_TIME_SIZE
rtcr_loop$:
            ld      c,(hl)
            in      a,(c)
            push    bc
            call    bcd_to_bin
            pop     bc
            ld      (de),a
            inc     de
            inc     hl
            djnz    rtcr_loop$
            call    drv_signal_done     ; ix = event (immediate completion)
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= rtc_write(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
rtc_write::
            call    rtc_check_time_len$
            ret     nz
            ld      hl,#rtc_time_ports$
            ld      b,#RTC_TIME_SIZE
rtcw_loop$:
            ld      a,(de)
            push    bc
            call    bin_to_bcd
            pop     bc
            ld      c,(hl)
            out     (c),a
            inc     de
            inc     hl
            djnz    rtcw_loop$
            call    drv_signal_done     ; ix = event (immediate completion)
            ld      hl,#DRV_OK
            ret

            ;; driver-level init: the mm58167 rtc needs none.
rtc_init::
            ld      hl,#DRV_OK
            ret

rtc_dev_drv::
            .dw     nvram_dev_drv
            .dw     0x0000
            .dw     rtc_init
            .dw     drv_open_ok
            .dw     drv_close_nop
            .dw     rtc_read
            .dw     rtc_write
            .dw     drv_ioctl_unsupported

rtc_dev::
rtc_dev$:
            .dw     0x0000
            .db     'r','t','c',0,0,0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     rtc_dev_drv

rtc_time_ports$:
            .db     RTC_PORT_SEC
            .db     RTC_PORT_MIN
            .db     RTC_PORT_HOUR
            .db     RTC_PORT_MDAY
            .db     RTC_PORT_MON
            .db     RTC_PORT_YEAR

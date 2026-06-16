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

            .include "drv.inc"
            .include "rtc.inc"

            .globl  bcd_to_bin
            .globl  bin_to_bcd
            .globl  drv_open_ok
            .globl  drv_close_nop
            .globl  drv_ioctl_unsupported

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *dev <= rtc_probe()
            ;; ----------------------------------------------------------------
            ;; mm58167 is mandatory on partner, so probe does not touch
            ;; hardware at all. it simply returns the one static rtc device.
            ;;
            ;; output(s):
            ;;  hl  ... rtc device
            ;; destroys:
            ;;  hl
            ;; ----------------------------------------------------------------
rtc_probe::
            ld      hl,#rtc_dev$
            ret

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
            in      a,(RTC_PORT_SEC)
            call    bcd_to_bin
            ld      (de),a
            inc     de
            in      a,(RTC_PORT_MIN)
            call    bcd_to_bin
            ld      (de),a
            inc     de
            in      a,(RTC_PORT_HOUR)
            call    bcd_to_bin
            ld      (de),a
            inc     de
            in      a,(RTC_PORT_MDAY)
            call    bcd_to_bin
            ld      (de),a
            inc     de
            in      a,(RTC_PORT_MON)
            call    bcd_to_bin
            ld      (de),a
            inc     de
            in      a,(RTC_PORT_YEAR)
            call    bcd_to_bin
            ld      (de),a
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= rtc_write(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
rtc_write::
            call    rtc_check_time_len$
            ret     nz
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_SEC),a
            inc     de
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_MIN),a
            inc     de
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_HOUR),a
            inc     de
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_MDAY),a
            inc     de
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_MON),a
            inc     de
            ld      a,(de)
            call    bin_to_bcd
            out     (RTC_PORT_YEAR),a
            ld      hl,#DRV_OK
            ret

rtc_dev_drv::
            .dw     0x0000
            .dw     rtc_probe
            .dw     drv_open_ok
            .dw     drv_close_nop
            .dw     rtc_read
            .dw     rtc_write
            .dw     drv_ioctl_unsupported

rtc_dev$:
            .dw     0x0000
            .db     'r','t','c',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     rtc_dev_drv

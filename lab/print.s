            ;; print.s
            ;;
            ;; minimal BIOS print_at helper
            ;;
            ;; print_at chooses the active text backend at runtime:
            ;;   - GDP/AVDC when a probed gdp device with AVDC is present
            ;;   - VT52-style serial output on the plain CRT Partner
            ;;
            ;; inputs:
            ;;  a   ... PRINT_ATTR_* selector
            ;;  b   ... x
            ;;  c   ... y
            ;;  hl  ... pointer to zero-terminated text
            ;; output(s):
            ;;  hl  ... DRV_OK / DRV_ERR
            ;; destroys:
            ;;  a, bc, de, hl
            ;;
            ;; 2026-06-13   tstih
            .module print

            .include "../../drivers/dev.inc"
            .include "../../drivers/drv.inc"
            .include "print.inc"
            .include "../../drivers/sio.inc"
            .include "../../drivers/gdp.inc"

            .globl  find_dev_drv
            .globl  gdp_write
            .globl  gdp_ioctl

            .area   _CODE

print_at::
            ld      (print_text_ptr$),hl
            ld      (print_attr_req$),a
            ld      a,b
            ld      (print_pos_buf$ + GDP_POS_X),a
            ld      a,c
            ld      (print_pos_buf$ + GDP_POS_Y),a

            ld      hl,#print_gdp_name$
            call    find_dev_drv
            jp      nz,print_crt$

            ld      (print_gdp_dev$),de
            push    de
            ld      hl,#DEV_DATA + GDP_DATA_FLAGS
            add     hl,de
            ld      a,(hl)
            pop     de
            and     #GDP_F_AVDC
            jp      z,print_crt$

            ld      a,(print_attr_req$)
            call    print_attr_to_gdp$
            ld      (print_new_attr$),a

            ld      hl,(print_gdp_dev$)
            ld      de,#print_old_attr$
            ld      bc,#GDP_IOCTL_GETATTR
            call    gdp_ioctl
            ld      a,h
            or      l
            jp      nz,print_fail$

            ld      hl,(print_gdp_dev$)
            ld      de,#print_pos_buf$
            ld      bc,#GDP_IOCTL_SETPOS
            call    gdp_ioctl
            ld      a,h
            or      l
            jp      nz,print_gdp_restore_fail$

            ld      hl,(print_gdp_dev$)
            ld      de,#print_new_attr$
            ld      bc,#GDP_IOCTL_SETATTR
            call    gdp_ioctl
            ld      a,h
            or      l
            jp      nz,print_gdp_restore_fail$

            call    print_strlen$
            ld      a,b
            or      c
            jr      z,print_gdp_restore_ok$

            ld      hl,(print_gdp_dev$)
            ld      de,(print_text_ptr$)
            call    gdp_write
            ld      a,h
            or      l
            jp      nz,print_gdp_restore_fail$

print_gdp_restore_ok$:
            ld      hl,(print_gdp_dev$)
            ld      de,#print_old_attr$
            ld      bc,#GDP_IOCTL_SETATTR
            call    gdp_ioctl
            ld      hl,#DRV_OK
            ret

print_gdp_restore_fail$:
            push    hl
            ld      hl,(print_gdp_dev$)
            ld      de,#print_old_attr$
            ld      bc,#GDP_IOCTL_SETATTR
            call    gdp_ioctl
            pop     hl
            ret

print_crt$:
            ld      a,(print_attr_req$)
            call    print_tty_attr$

            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'Y'
            call    print_tty_putc$
            ld      a,(print_pos_buf$ + GDP_POS_Y)
            add     a,#32
            call    print_tty_putc$
            ld      a,(print_pos_buf$ + GDP_POS_X)
            add     a,#32
            call    print_tty_putc$

            ld      hl,(print_text_ptr$)
print_crt_loop$:
            ld      a,(hl)
            or      a
            jr      z,print_crt_done$
            inc     hl
            call    print_tty_putc$
            jr      print_crt_loop$

print_crt_done$:
            ld      a,#PRINT_ATTR_NORMAL
            call    print_tty_attr$
            ld      hl,#DRV_OK
            ret

print_fail$:
            ret

print_strlen$:
            ld      hl,(print_text_ptr$)
            ld      bc,#0x0000
print_strlen_loop$:
            ld      a,(hl)
            or      a
            ret     z
            inc     hl
            inc     bc
            jr      print_strlen_loop$

print_attr_to_gdp$:
            cp      #PRINT_ATTR_HIGHLIGHT
            jr      z,print_attr_gdp_hi$
            cp      #PRINT_ATTR_INVERSE
            jr      z,print_attr_gdp_inv$
            xor     a
            ret
print_attr_gdp_hi$:
            ld      a,#GDP_ATTR_HIGHLIGHT
            ret
print_attr_gdp_inv$:
            ld      a,#GDP_ATTR_INVERSE
            ret

print_tty_attr$:
            cp      #PRINT_ATTR_NORMAL
            jr      z,print_tty_attr_norm$
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'p'
            jp      print_tty_putc$
print_tty_attr_norm$:
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'q'
            jp      print_tty_putc$

print_tty_putc$:
            ld      (print_ch$),a
print_tty_wait$:
            in      a,(SIO0A_CTRL_PORT)
            and     #SIO_RR0_TX_EMPTY
            jr      z,print_tty_wait$
            ld      a,(print_ch$)
            out     (SIO0A_DATA_PORT),a
            ret

print_gdp_name$:
            .db     'g','d','p',0

            .area   _SYSVARS
print_text_ptr$:
            .ds     2
print_gdp_dev$:
            .ds     2
print_pos_buf$:
            .ds     GDP_POS_SIZE
print_old_attr$:
            .ds     1
print_new_attr$:
            .ds     1
print_attr_req$:
            .ds     1
print_ch$:
            .ds     1

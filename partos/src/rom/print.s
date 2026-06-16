            ;; print.s
            ;;
            ;; minimal rom-side print_at dispatcher.
            ;;
            ;; output policy is intentionally cheap:
            ;;   - GDP models print to AVDC only
            ;;   - text models choose the first configured TERMINAL port
            ;;   - invalid setup falls back to the default SIO0A console
            ;;
            ;; native calling convention only:
            ;;
            ;;   print_at():
            ;;     in : a  = PRINT_ATTR_* selector
            ;;          b  = x
            ;;          c  = y
            ;;          hl = zero-terminated text
            ;;
            ;; 2026-06-14   tstih
            .module print

            .include "../drivers/sio.inc"
            .include "print.inc"

            .globl  avdc_print_at
            .globl  bios_nvram_cache
            .globl  bios_sio_init_block
            .globl  model
            .globl  print_at

            .equ    PRINT_TTYS_ATTACH_BYTE,     3
            .equ    PRINT_TTYS0_MASK,           0xc0
            .equ    PRINT_TTYS1_MASK,           0x30
            .equ    PRINT_TTYS2_MASK,           0x0c
            .equ    PRINT_TTYS3_MASK,           0x03
            .equ    PRINT_TTYS0_TERMINAL,       0x40
            .equ    PRINT_TTYS1_TERMINAL,       0x10
            .equ    PRINT_TTYS2_TERMINAL,       0x04
            .equ    PRINT_TTYS3_TERMINAL,       0x01

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; print_at(<a> attr, <b> x, <c> y, <hl> *text)
            ;; ----------------------------------------------------------------
print_at::
            ld      d,a
            ld      a,(model)
            or      a
            ld      a,d
            jp      nz,avdc_print_at

            push    hl
            push    bc
            push    af

            ld      d,#SIO0A_DATA_PORT
            ld      e,#SIO0A_CTRL_PORT
            ld      a,(bios_nvram_cache + PRINT_TTYS_ATTACH_BYTE)
            ld      c,a
            and     #PRINT_TTYS0_MASK
            cp      #PRINT_TTYS0_TERMINAL
            jr      z,print_init$

            ld      a,c
            and     #PRINT_TTYS1_MASK
            cp      #PRINT_TTYS1_TERMINAL
            jr      nz,print_ttys2$
            ld      d,#SIO0B_DATA_PORT
            ld      e,#SIO0B_CTRL_PORT
            jr      print_init$

print_ttys2$:
            ld      a,c
            and     #PRINT_TTYS2_MASK
            cp      #PRINT_TTYS2_TERMINAL
            jr      nz,print_ttys3$
            ld      d,#SIO1A_DATA_PORT
            ld      e,#SIO1A_CTRL_PORT
            jr      print_init$

print_ttys3$:
            ld      a,c
            and     #PRINT_TTYS3_MASK
            cp      #PRINT_TTYS3_TERMINAL
            jr      nz,print_init$
            ld      d,#SIO1B_DATA_PORT
            ld      e,#SIO1B_CTRL_PORT

print_init$:
            ld      c,e
            ld      hl,#bios_sio_init_block
            ld      b,#7
            otir

            pop     af
            pop     bc
            pop     hl

print_tty_port$:
            push    bc
            call    print_tty_attr$
            pop     bc
            push    bc
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'Y'
            call    print_tty_putc$
            pop     bc
            push    bc
            ld      a,c
            add     a,#32
            call    print_tty_putc$
            pop     bc
            ld      a,b
            add     a,#32
            call    print_tty_putc$

print_tty_loop$:
            ld      a,(hl)
            or      a
            ret     z
            inc     hl
            call    print_tty_putc$
            jr      print_tty_loop$

print_tty_attr$:
            cp      #PRINT_ATTR_NORMAL
            jr      z,print_tty_attr_norm$
            cp      #PRINT_ATTR_INVERSE
            jr      z,print_tty_attr_inv$

            ;; private CRT text attribute extension:
            ;;   ESC b '1' = highlight
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'b'
            call    print_tty_putc$
            ld      a,#'1'
            jp      print_tty_putc$

print_tty_attr_norm$:
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'q'
            jp      print_tty_putc$

print_tty_attr_inv$:
            ld      a,#0x1b
            call    print_tty_putc$
            ld      a,#'p'
            jp      print_tty_putc$

print_tty_putc$:
            ld      b,a
            ld      c,e
print_tty_wait$:
            in      a,(c)
            and     #SIO_RR0_TX_EMPTY
            jr      z,print_tty_wait$
            ld      c,d
            ld      a,b
            out     (c),a
            ret

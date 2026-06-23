            ;; console.s
            ;;
            ;; small OS-owned console bridge for the public "partos" service.
            ;;
            ;; policy:
            ;;   - graphics models print through the AVDC device
            ;;   - text models print through the first configured TERMINAL SIO
            ;;   - keyboard input is polled from SIO0A (the common keyboard path
            ;;     in the current emulator + ROM model)
            ;;
            ;; the public calls are intentionally tiny:
            ;;   clear_screen()
            ;;   set_xy(avdc_xy_t *xy)
            ;;   write_console(buf, len)
            ;;   peek_keyboard() -> -1 / char
            ;;   read_keyboard() -> blocks until a char is available
            ;;
            ;; 2026-06-22   tstih
            .module console

            .include "../drivers/avdc.inc"
            .include "../drivers/drv.inc"
            .include "../drivers/sio.inc"

            .globl  _console_init
            .globl  _partos_clear_screen
            .globl  _partos_set_xy
            .globl  _partos_write_console
            .globl  _partos_peek_keyboard
            .globl  _partos_read_keyboard

            .globl  avdc_open
            .globl  avdc_write
            .globl  avdc_ioctl
            .globl  avdc_dev0
            .globl  __sys_model
            .globl  __sys_nvram_cache

            .equ    MODEL_F_GRAPHICS,          0x01
            .equ    CONSOLE_MODE_TTY,          0
            .equ    CONSOLE_MODE_AVDC,         1
            .equ    CONSOLE_TTYS_ATTACH_BYTE,  3
            .equ    CONSOLE_TTYS0_MASK,        0xc0
            .equ    CONSOLE_TTYS1_MASK,        0x30
            .equ    CONSOLE_TTYS2_MASK,        0x0c
            .equ    CONSOLE_TTYS3_MASK,        0x03
            .equ    CONSOLE_TTYS0_TERMINAL,    0x40
            .equ    CONSOLE_TTYS1_TERMINAL,    0x10
            .equ    CONSOLE_TTYS2_TERMINAL,    0x04
            .equ    CONSOLE_TTYS3_TERMINAL,    0x01

            .area   _CODE

console_ok$:
            ld      de,#DRV_OK
            ret

console_tty_putc$:
            push    bc                  ; callers keep lengths/coords in bc
            ld      b,a
            ld      a,(console_tx_ctrl$)
            ld      c,a
ctpw_wait$:
            in      a,(c)
            and     #SIO_RR0_TX_EMPTY
            jr      z,ctpw_wait$
            ld      a,(console_tx_data$)
            ld      c,a
            ld      a,b
            out     (c),a
            pop     bc
            ret

console_tty_init_ctrl$:
            ld      c,a
            ld      hl,#console_sio_init$
            ld      b,#7
            otir
            ret

console_choose_tty$:
            ld      d,#SIO0A_DATA_PORT
            ld      e,#SIO0A_CTRL_PORT
            ld      a,(__sys_nvram_cache + CONSOLE_TTYS_ATTACH_BYTE)
            ld      c,a
            and     #CONSOLE_TTYS0_MASK
            cp      #CONSOLE_TTYS0_TERMINAL
            ret     z

            ld      a,c
            and     #CONSOLE_TTYS1_MASK
            cp      #CONSOLE_TTYS1_TERMINAL
            jr      nz,console_tty2$
            ld      d,#SIO0B_DATA_PORT
            ld      e,#SIO0B_CTRL_PORT
            ret

console_tty2$:
            ld      a,c
            and     #CONSOLE_TTYS2_MASK
            cp      #CONSOLE_TTYS2_TERMINAL
            jr      nz,console_tty3$
            ld      d,#SIO1A_DATA_PORT
            ld      e,#SIO1A_CTRL_PORT
            ret

console_tty3$:
            ld      a,c
            and     #CONSOLE_TTYS3_MASK
            cp      #CONSOLE_TTYS3_TERMINAL
            ret     nz
            ld      d,#SIO1B_DATA_PORT
            ld      e,#SIO1B_CTRL_PORT
            ret

_console_init::
            ;; keyboard path is always prepared on sio0a.
            ld      a,#SIO0A_CTRL_PORT
            call    console_tty_init_ctrl$

            xor     a
            ld      (console_mode$),a
            ld      (console_dev$),a
            ld      (console_dev$ + 1),a

            ld      a,(__sys_model)
            and     #MODEL_F_GRAPHICS
            jr      z,console_text$

            ld      hl,#avdc_dev0
            call    avdc_open
            ld      a,h
            or      l
            jr      nz,console_text$
            ld      hl,#avdc_dev0
            ld      (console_dev$),hl
            ld      a,#CONSOLE_MODE_AVDC
            ld      (console_mode$),a
            ret

console_text$:
            call    console_choose_tty$
            ld      a,d
            ld      (console_tx_data$),a
            ld      a,e
            ld      (console_tx_ctrl$),a
            ld      a,e
            call    console_tty_init_ctrl$
            ret

_partos_clear_screen::
            ld      a,(console_mode$)
            cp      #CONSOLE_MODE_AVDC
            jr      z,pcs_avdc$
            ld      a,#0x1b
            call    console_tty_putc$
            ld      a,#'E'
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

pcs_avdc$:
            ld      hl,(console_dev$)
            ld      de,#0x0000
            ld      bc,#AVDC_IOCTL_CLEAR
            call    avdc_ioctl
            ex      de,hl
            ret

_partos_set_xy::
            ld      a,(console_mode$)
            cp      #CONSOLE_MODE_AVDC
            jr      z,psx_avdc$

            ld      c,(hl)              ; x
            inc     hl
            ld      b,(hl)              ; y
            ld      a,#0x1b
            call    console_tty_putc$
            ld      a,#'Y'
            call    console_tty_putc$
            ld      a,b
            add     a,#32
            call    console_tty_putc$
            ld      a,c
            add     a,#32
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

psx_avdc$:
            ex      de,hl               ; de = xy
            ld      hl,(console_dev$)
            ld      bc,#AVDC_IOCTL_GOTOXY
            call    avdc_ioctl
            ex      de,hl
            ret

_partos_write_console::
            ld      a,(console_mode$)
            cp      #CONSOLE_MODE_AVDC
            jr      z,pwc_avdc$

            ld      b,d
            ld      c,e
pwc_tty_loop$:
            ld      a,b
            or      c
            jr      z,pwc_tty_done$
            ld      a,(hl)
            inc     hl
            call    console_tty_putc$
            dec     bc
            jr      pwc_tty_loop$
pwc_tty_done$:
            ld      de,#DRV_OK
            ret

pwc_avdc$:
            push    hl                  ; save buf
            ld      b,d
            ld      c,e
            pop     de                  ; de = buf
            ld      hl,(console_dev$)
            ld      ix,#0x0000
            call    avdc_write
            ex      de,hl
            ret

_partos_peek_keyboard::
            ;; Console input is a blocking/polling userland boundary. If a
            ;; caller reaches it with maskable interrupts still off, the shell
            ;; can monopolize the CPU and starve newly created runnable
            ;; threads. Keep keyboard polling preemptible.
            ei
            in      a,(SIO0A_CTRL_PORT)
            and     #SIO_RR0_RX_AVAIL
            jr      nz,ppk_have$
            ld      de,#0xffff
            ret
ppk_have$:
            in      a,(SIO0A_DATA_PORT)
            ld      e,a
            ld      d,#0x00
            ret

_partos_read_keyboard::
            ;; Same rule as peek_keyboard(): a user thread blocked on keyboard
            ;; input must leave the periodic tick free to schedule other
            ;; runnable threads. Re-arm maskable interrupts on every polling
            ;; pass so a prior interrupt accept cannot strand us in the loop
            ;; with IFF1 cleared again.
prk_wait$:
            ei
            in      a,(SIO0A_CTRL_PORT)
            and     #SIO_RR0_RX_AVAIL
            jr      z,prk_wait$
            in      a,(SIO0A_DATA_PORT)
            ld      e,a
            ld      d,#0x00
            ret

            .area   _INITIALIZED

console_sio_init$:
            .db     0x18,0x04,0x44,0x03,0xc1,0x05,0x68

            .area   _SYSVARS

console_mode$:
            .ds     1
console_tx_data$:
            .ds     1
console_tx_ctrl$:
            .ds     1
console_dev$:
            .ds     2

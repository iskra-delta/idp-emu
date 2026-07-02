            ;; console.s
            ;;
            ;; small OS-owned console bridge for the public "partos" service.
            ;;
            ;; policy:
            ;;   - graphics models print through the AVDC device
            ;;   - text models print through the first configured TERMINAL SIO
            ;;   - keyboard input is read through the SIO driver (SIO0A channel)
            ;;
            ;; keyboard input goes through the SIO driver's synchronous polled
            ;; ioctls (PEEK / GETC, see drivers/sio.inc) instead of poking the SIO
            ;; ports here. text output still uses the small inline polled TX below:
            ;; routing text output through the driver's PUTC/TTYINIT ioctls also
            ;; works electrically, but it perturbs the OS layout enough to trip a
            ;; latent (nondeterministic-`-Os`-userland) command-execution bug, so
            ;; it is deferred until that userland issue is resolved.
            ;;
            ;; the public calls are intentionally tiny:
            ;;   clear_screen()
            ;;   set_xy(avdc_xy_t *xy)
            ;;   write_console(buf, len)
            ;;   set_text_attr(attr)
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
            .globl  _partos_set_text_attr
            .globl  _partos_peek_keyboard
            .globl  _partos_read_keyboard

            .globl  avdc_open
            .globl  avdc_write
            .globl  avdc_ioctl
            .globl  avdc_dev0
            .globl  __sys_model
            .globl  __sys_nvram_cache
            .globl  sio_ioctl
            .globl  sio_dev0

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
            ld      b,#9
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
            ld      (console_attr$),a
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
            push    ix                  ; SDCC C callers may keep a live frame
                                        ; pointer in IX across the service call.
            ld      ix,#0x0000
            call    avdc_write
            pop     ix
            ex      de,hl
            ret

_partos_set_text_attr::
            and     #AVDC_ATTR_MASK
            ld      (console_attr$),a
            ld      b,a
            ld      a,(console_mode$)
            cp      #CONSOLE_MODE_AVDC
            jr      z,psta_avdc$
            ld      a,b
            cp      #AVDC_ATTR_HIGHLIGHT
            jr      z,psta_tty_hi$
            cp      #AVDC_ATTR_INVERSE
            jr      z,psta_tty_inv$
            cp      #AVDC_ATTR_UNDERLINE
            jr      z,psta_tty_ul$
            ld      a,#0x1b            ; normal -> ESC q
            call    console_tty_putc$
            ld      a,#'q'
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

psta_tty_hi$:
            ld      a,#0x1b            ; highlight -> ESC b '1'
            call    console_tty_putc$
            ld      a,#'b'
            call    console_tty_putc$
            ld      a,#'1'
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

psta_tty_inv$:
            ld      a,#0x1b            ; inverse -> ESC p
            call    console_tty_putc$
            ld      a,#'p'
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

psta_tty_ul$:
            ld      a,#0x1b            ; underline -> ESC [ 4 m
            call    console_tty_putc$
            ld      a,#'['
            call    console_tty_putc$
            ld      a,#'4'
            call    console_tty_putc$
            ld      a,#'m'
            call    console_tty_putc$
            ld      de,#DRV_OK
            ret

psta_avdc$:
            ld      hl,(console_dev$)
            ld      de,#console_attr$
            ld      bc,#AVDC_IOCTL_SETATTR
            call    avdc_ioctl
            ex      de,hl
            ret

_partos_peek_keyboard::
            ;; non-blocking keyboard poll through the SIO driver (its PEEK
            ;; re-arms interrupts so a polling console stays preemptible). result
            ;; in de; hl/bc/ix preserved (the hand-written shell keeps its input
            ;; buffer pointer/count in them across this call).
            push    ix
            push    hl
            push    bc
            ld      hl,#sio_dev0
            ld      bc,#SIO_IOCTL_PEEK
            call    sio_ioctl           ; hl = char / 0xffff
            ex      de,hl
            pop     bc
            pop     hl
            pop     ix
            ret

_partos_read_keyboard::
            ;; blocking keyboard read through the SIO driver (its GETC re-arms
            ;; interrupts on every polling pass). same register contract as peek.
            push    ix
            push    hl
            push    bc
            ld      hl,#sio_dev0
            ld      bc,#SIO_IOCTL_GETC
            call    sio_ioctl           ; hl = char
            ex      de,hl
            pop     bc
            pop     hl
            pop     ix
            ret

            .area   _INITIALIZED

console_sio_init$:
            .db     0x18,0x04,0x44,0x03,0xc1,0x05,0x68,0x01,0x00

            .area   _SYSVARS

console_mode$:
            .ds     1
console_tx_data$:
            .ds     1
console_tx_ctrl$:
            .ds     1
console_attr$:
            .ds     1
console_dev$:
            .ds     2

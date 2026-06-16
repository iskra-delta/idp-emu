            ;; kbd.s
            ;;
            ;; minimal rom-side keyboard support.
            ;;
            ;; the Partner keyboard is wired to SIO chip 0, channel A on all
            ;; known models. these routines use a tiny native ABI only:
            ;;
            ;;   kbd_init():
            ;;     programs SIO0 channel A for keyboard input.
            ;;
            ;;   kbd_read():
            ;;     returns one byte in a, or 0 if no key is available.
            ;;
            ;; 2026-06-14   tstih
            .module kbd

            .include "../drivers/sio.inc"

            .globl  bios_sio_init_block
            .globl  kbd_init
            .globl  kbd_read

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; kbd_init()
            ;; ----------------------------------------------------------------
            ;; initializes SIO0 channel A with the same compact setup already
            ;; used elsewhere in PartOS for early serial paths.
            ;; destroys:
            ;;   a, b, c, hl
            ;; ----------------------------------------------------------------
kbd_init::
            ;; send the compact 7-byte setup script directly to sio0a control
            ld      c,#SIO0A_CTRL_PORT
            ld      hl,#bios_sio_init_block
            ld      b,#7
            otir
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= kbd_read()
            ;; ----------------------------------------------------------------
            ;; checks whether one keyboard byte is pending in SIO0A.
            ;; returns 0 if no key is available.
            ;; destroys:
            ;;   flags
            ;; ----------------------------------------------------------------
kbd_read::
            ;; rr0 tells us whether a receive byte is waiting right now
            in      a,(SIO0A_CTRL_PORT)
            and     #SIO_RR0_RX_AVAIL
            jr      nz,kbd_read_go$
            xor     a
            ret
kbd_read_go$:
            ;; once data is pending, fetch one raw keyboard byte
            in      a,(SIO0A_DATA_PORT)
            ret

bios_sio_init_block::
            .db     0x18,0x04,0x44,0x03,0xc1,0x05,0x68

            ;; sio.s
            ;;
            ;; partner z80 sio bios driver (probe only for now)
            ;;
            ;; 2026-06-13   tstih
            .module sio

            .include "dev.inc"
            .include "drv.inc"
            .include "sio.inc"

            .globl  drv_reset_dev
            .globl  drv_close_nop
            .globl  drv_read_unsupported
            .globl  drv_write_unsupported
            .globl  drv_ioctl_unsupported

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *chain <= sio_probe()
            ;; ----------------------------------------------------------------
            ;; probes the raw sio control ports. chip 0 is normally present,
            ;; while chip 1 may be optional on some machines. a responding chip
            ;; publishes its two channels as ttyS0/ttyS1 or ttyS2/ttyS3. each
            ;; device keeps its raw data/control ports plus chip/channel indices
            ;; in dev.data[] for later real serial I/O work.
            ;;
            ;; output(s):
            ;;  hl  ... head of the tty device chain, 0x0000 if no sio found
            ;; destroys:
            ;;  a, bc, de
            ;; ----------------------------------------------------------------
sio_open::
            push    hl
            ld      bc,#DEV_FLAGS
            add     hl,bc
            bit     0,(hl)
            jr      z,sio_open_go$
            pop     hl
            ld      hl,#DRV_OK
            ret
sio_open_go$:
            set     0,(hl)
            pop     hl

            push    hl
            ld      bc,#DEV_DATA + SIO_CTRL_PORT_OFF
            add     hl,bc
            ld      c,(hl)
            ld      hl,#sio_init_block$
            ld      b,#7
            otir
            pop     hl

            ld      hl,#DRV_OK
            ret

sio_probe::
            xor     a
            ld      l,a
            ld      h,a
            ld      (sio_probe_head$),hl
            ld      (sio_probe_tail$),hl

            in      a,(SIO0A_CTRL_PORT)
            cp      #0xff
            jr      z,sio_chip1$
            ld      hl,#sio_dev0$
            ld      de,#sio_dev_drv
            call    drv_reset_dev
            call    sio_init_dev0$
            call    sio_append_dev$
            ld      hl,#sio_dev1$
            ld      de,#sio_dev_drv
            call    drv_reset_dev
            call    sio_init_dev1$
            call    sio_append_dev$

sio_chip1$:
            in      a,(SIO1A_CTRL_PORT)
            cp      #0xff
            jr      z,sio_done$
            ld      hl,#sio_dev2$
            ld      de,#sio_dev_drv
            call    drv_reset_dev
            call    sio_init_dev2$
            call    sio_append_dev$
            ld      hl,#sio_dev3$
            ld      de,#sio_dev_drv
            call    drv_reset_dev
            call    sio_init_dev3$
            call    sio_append_dev$

sio_done$:
            ld      hl,(sio_probe_head$)
            ret

sio_append_dev$:
            ex      de,hl               ; de = dev_t*

            ld      hl,(sio_probe_head$)
            ld      a,h
            or      l
            jr      nz,sio_link$
            ld      (sio_probe_head$),de
            jr      sio_tail$
sio_link$:
            ld      hl,(sio_probe_tail$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
sio_tail$:
            ld      (sio_probe_tail$),de
            ret

sio_init_dev0$:
            ld      a,#SIO0A_DATA_PORT
            ld      b,#SIO0A_CTRL_PORT
            ld      c,#0
            ld      d,#0
            jr      sio_init_dev$

sio_init_dev1$:
            ld      a,#SIO0B_DATA_PORT
            ld      b,#SIO0B_CTRL_PORT
            ld      c,#0
            ld      d,#1
            jr      sio_init_dev$

sio_init_dev2$:
            ld      a,#SIO1A_DATA_PORT
            ld      b,#SIO1A_CTRL_PORT
            ld      c,#1
            ld      d,#0
            jr      sio_init_dev$

sio_init_dev3$:
            ld      a,#SIO1B_DATA_PORT
            ld      b,#SIO1B_CTRL_PORT
            ld      c,#1
            ld      d,#1

sio_init_dev$:
            push    hl
            ld      de,#DEV_DATA
            add     hl,de
            ld      (hl),a
            inc     hl
            ld      (hl),b
            inc     hl
            ld      (hl),c
            inc     hl
            ld      (hl),d
            pop     hl
            ret

sio_dev_drv::
            .dw     0x0000
            .dw     sio_probe
            .dw     sio_open
            .dw     drv_close_nop
            .dw     drv_read_unsupported
            .dw     drv_write_unsupported
            .dw     drv_ioctl_unsupported

sio_init_block$:
            .db     0x18,0x04,0x44,0x03,0xc1,0x05,0x68

sio_dev0$:
            .dw     0x0000
            .db     't','t','y','S','0',0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     sio_dev_drv

sio_dev1$:
            .dw     0x0000
            .db     't','t','y','S','1',0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     sio_dev_drv

sio_dev2$:
            .dw     0x0000
            .db     't','t','y','S','2',0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     sio_dev_drv

sio_dev3$:
            .dw     0x0000
            .db     't','t','y','S','3',0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     sio_dev_drv

sio_probe_head$:
            .dw     0x0000

sio_probe_tail$:
            .dw     0x0000

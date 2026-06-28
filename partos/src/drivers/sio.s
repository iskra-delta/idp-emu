            ;; sio.s
            ;;
            ;; partner z80 sio stream driver
            ;;
            ;; each published ttyS* channel is now a real async character
            ;; device:
            ;;   - incoming bytes are pulled by the SIO interrupt handler into a
            ;;     software RX ring (or directly into one waiting read request)
            ;;   - outgoing bytes are staged in a software TX queue and written
            ;;     to the SIO only when the chip raises "tx buffer empty"
            ;;   - one pending read request and one pending write request are
            ;;     supported per channel; lock ioctls let one owner reserve the
            ;;     device and therefore avoid multi-process request queueing
            ;;   - line setup is binary, not text-parsed: applications pass one
            ;;     tiny struct with baud / data bits / parity / stop bits
            ;;
            ;; the current machine model does not derive per-channel baud clocks
            ;; from the CTC yet, so baud is validated and remembered at the ABI
            ;; level while WR4 stays on the emulator-friendly x16 clock mode.
            ;;
            ;; 2026-06-17   tstih
            .module sio

            .include "dev.inc"
            .include "drv.inc"
            .include "sio.inc"

            .globl  drv_close_nop
            .globl  drv_signal_done
            .globl  drv_zero_ok_bc_ix
            .globl  drv_owner_current
            .globl  drv_state_by_dev_ix_a
            .globl  drv_owner_guard_ix
            .globl  drv_alloc_buffers_ix
            .globl  drv_read_drain_ring
            .globl  drv_tx_refill
            .globl  drv_stream_read_ix
            .globl  drv_stream_write_ix
            .globl  drv_update_busy_ix
            .globl  drv_update_lock_ix
            .globl  drv_set_error_ix
            .globl  drv_isr_enter
            .globl  drv_isr_exit
            .globl  drv_complete_write_ix
            .globl  drv_complete_read_ix
            .globl  drv_complete_write_busy_ix
            .globl  drv_complete_read_busy_ix
            .globl  drv_purge_owner_ix
            .globl  drv_lock_ix
            .globl  drv_unlock_ix
            .globl  drv_setbufs_ix
            .globl  drv_err
            .globl  __sys_nvram_cache
            .globl  sio_init
            .globl  sio_dev0
            .globl  sio_dev1
            .globl  sio_dev2
            .globl  sio_dev3
            .globl  _ir_set
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  ir_refcnt
            .globl  _owner_cleanup_register
            .globl  _thread_current
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  __sys_heap
            .globl  _sio0_isr
            .globl  _sio1_isr
            .globl  _sio_purge_owner
            .globl  pio_dev_drv

            .equ    THREAD_PROCESS,      22

            ;; nvram byte 3 packs the serial attach selectors (0 = absent):
            ;;   ttyS0[7:6] ttyS1[5:4] ttyS2[3:2] ttyS3[1:0]
            .equ    SIO_NVRAM_ATTACH_BYTE,  3

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; sio_open(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; open is intentionally light-weight: allocate the heap-backed rx/tx
            ;; buffers on first use, (re)program the line registers and mark the
            ;; device open. if another owner has locked the channel, open fails.
            ;; ----------------------------------------------------------------
sio_open::
            call    sio_owner_guard$
            jp      c,drv_err
            call    drv_alloc_buffers_ix
            jp      c,drv_err
            call    _ir_disable
            call    sio_program_line$
            ld      l,SIO_ST_DEV(ix)
            ld      h,SIO_ST_DEV+1(ix)
            ld      de,#DEV_FLAGS
            add     hl,de
            set     0,(hl)
            res     2,(hl)
            call    _ir_enable
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; sio_init()
            ;; ----------------------------------------------------------------
            ;; install one IM2 vector per physical SIO chip, register the
            ;; owner-death cleanup hook and seed WR2 on channel B of both chips.
            ;; per-channel resets + wr1/wr3/wr4/wr5 programming are deferred to
            ;; sio_open() so applications can change the line config first.
            ;; ----------------------------------------------------------------
sio_init::
            ld      hl,#sio_dev0$
            call    sio_init_dev0$
            ld      hl,#sio_dev1$
            call    sio_init_dev1$
            ld      hl,#sio_dev2$
            call    sio_init_dev2$
            ld      hl,#sio_dev3$
            call    sio_init_dev3$
            ;; quiesce both physical SIO chips first so no ROM-era WR1/interrupt
            ;; state leaks into the OS's first `ei`. the async tty driver later
            ;; re-enables WR1 interrupts in sio_open() when a channel is actually
            ;; opened for buffered I/O.
            ld      c,#SIO0A_CTRL_PORT
            ld      a,#SIO_WRCMD_RESET
            out     (c),a
            ld      c,#SIO0B_CTRL_PORT
            out     (c),a
            ld      c,#SIO1A_CTRL_PORT
            out     (c),a
            ld      c,#SIO1B_CTRL_PORT
            out     (c),a
            ld      a,#SIO0_VEC
            ld      de,#_sio0_isr
            call    _ir_set
            ld      a,#SIO1_VEC
            ld      de,#_sio1_isr
            call    _ir_set
            ld      hl,#_sio_purge_owner
            call    _owner_cleanup_register
            ld      c,#SIO0B_CTRL_PORT
            ld      a,#2
            out     (c),a
            ld      a,#SIO0_VEC
            out     (c),a
            ld      c,#SIO1B_CTRL_PORT
            ld      a,#2
            out     (c),a
            ld      a,#SIO1_VEC
            out     (c),a
            ld      hl,#DRV_OK
            ret

sio_init_dev0$:
            ld      a,#SIO0A_DATA_PORT
            ld      b,#SIO0A_CTRL_PORT
            ld      de,#sio_state0$
            jr      sio_init_dev$

sio_init_dev1$:
            ld      a,#SIO0B_DATA_PORT
            ld      b,#SIO0B_CTRL_PORT
            ld      de,#sio_state1$
            jr      sio_init_dev$

sio_init_dev2$:
            ld      a,#SIO1A_DATA_PORT
            ld      b,#SIO1A_CTRL_PORT
            ld      de,#sio_state2$
            jr      sio_init_dev$

sio_init_dev3$:
            ld      a,#SIO1B_DATA_PORT
            ld      b,#SIO1B_CTRL_PORT
            ld      de,#sio_state3$

sio_init_dev$:                          ; hl=dev, a=data, b=ctrl, de=state
            push    hl
            push    de
            ld      bc,#DEV_DATA
            add     hl,bc
            ld      (hl),a
            inc     hl
            ld      (hl),b
            inc     hl
            ld      a,b
            cp      #SIO1A_CTRL_PORT
            jr      nc,sid_chip1$
            xor     a
            jr      sid_chip$
sid_chip1$:
            ld      a,#1
sid_chip$:
            ld      (hl),a              ; chip id
            inc     hl
            ld      a,b
            and     #0x02
            srl     a
            ld      (hl),a              ; chan id
            inc     hl
            pop     de
            ld      (hl),e              ; state ptr lo
            inc     hl
            ld      (hl),d              ; state ptr hi
            pop     hl

            push    hl
            push    de
            ex      de,hl               ; hl = state
            xor     a
            ld      b,#SIO_ST_SIZE
sid_zero$:
            ld      (hl),a
            inc     hl
            djnz    sid_zero$
            pop     de                  ; de = state
            pop     hl                  ; hl = dev
            push    de
            pop     ix
            ld      SIO_ST_DEV(ix),l
            ld      SIO_ST_DEV+1(ix),h
            ld      SIO_ST_DATA(ix),a   ; overwritten below
            ld      bc,#DEV_DATA
            add     hl,bc
            ld      a,(hl)
            ld      SIO_ST_DATA(ix),a
            inc     hl
            ld      a,(hl)
            ld      SIO_ST_CTRL(ix),a
            ld      a,#SIO_DEFAULT_RX
            ld      SIO_ST_RXSZ(ix),a
            ld      a,#SIO_DEFAULT_TX
            ld      SIO_ST_TXSZ(ix),a
            ld      a,#SIO_WR3_DEFAULT
            ld      SIO_ST_WR3(ix),a
            ld      a,#SIO_WR4_DEFAULT
            ld      SIO_ST_WR4(ix),a
            ld      a,#SIO_WR5_DEFAULT
            ld      SIO_ST_WR5(ix),a
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= sio_read (<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; drains already-buffered RX bytes first. if the caller still wants
            ;; more data afterwards, the remainder becomes the single pending
            ;; read request for this channel and the interrupt handler fills it.
            ;; ----------------------------------------------------------------
sio_read::
            call    drv_zero_ok_bc_ix
            ret     z
            push    ix                  ; preserve caller event pointer
            call    sio_owner_guard$
            jr      c,sior_fail$
            call    drv_stream_read_ix
            ret     nc
            pop     ix
sior_fail$:
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= sio_write(<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; copies as much data as fits into the TX queue, kicks the first
            ;; byte if the SIO is idle, and leaves the rest for later TX-empty
            ;; interrupts to drain. one write request may be active at a time.
            ;; ----------------------------------------------------------------
sio_write::
            call    drv_zero_ok_bc_ix
            ret     z
            push    ix                  ; preserve caller event pointer
            call    sio_owner_guard$
            jr      c,siow_fail$
            call    drv_stream_write_ix
            jr      c,siow_fail_pop$
            call    sio_tx_kick$
            call    drv_update_busy_ix
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
siow_fail_pop$:
            pop     ix
siow_fail$:
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= sio_ioctl(<hl> dev, <de> params, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; 0x20 set buffer sizes before first open/allocation
            ;; 0x21 lock to current owner
            ;; 0x22 unlock from current owner
            ;; 0x23 update line parameters from sio_linecfg_t
            ;; ----------------------------------------------------------------
sio_ioctl::
            ld      a,b
            or      a
            jp      nz,drv_err
            call    sio_state_by_dev$
            ld      a,c
            cp      #SIO_IOCTL_LOCK
            jp      z,drv_lock_ix
            cp      #SIO_IOCTL_UNLOCK
            jp      z,drv_unlock_ix
            call    drv_owner_guard_ix
            jp      c,drv_err
            ld      a,c
            cp      #SIO_IOCTL_SETBUFS
            jp      z,drv_setbufs_ix
            cp      #SIO_IOCTL_INITLINE
            jr      z,sio_initline$
            jp      drv_err

sio_initline$:
            call    sio_line_from_cfg$
            jp      c,drv_err
            call    _ir_disable
            ld      SIO_ST_WR3(ix),b
            ld      SIO_ST_WR4(ix),c
            ld      SIO_ST_WR5(ix),d
            ld      a,SIO_ST_DEV(ix)
            ld      h,SIO_ST_DEV+1(ix)
            ld      l,a
            ld      de,#DEV_FLAGS
            add     hl,de
            bit     0,(hl)
            jr      z,sio_initline_done$
            call    sio_program_line$
sio_initline_done$:
            call    _ir_enable
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; sio helpers
            ;; ----------------------------------------------------------------
sio_state_by_dev$:                      ; hl=dev -> ix=state, hl preserved
            ld      a,#DEV_DATA + SIO_STATE_PTR_OFF
            jp      drv_state_by_dev_ix_a

sio_owner_guard$:                       ; hl=dev -> cf=1 if locked by other
            call    sio_state_by_dev$
            jp      drv_owner_guard_ix

sio_program_line$:                      ; ix=state
            ld      c,SIO_ST_CTRL(ix)
            ld      a,#SIO_WRCMD_RESET
            out     (c),a
            bit     1,c
            jr      z,sio_prog_wr4$
            ld      a,#2
            out     (c),a
            ld      a,#SIO0_VEC
            bit     5,c
            jr      z,sio_prog_vec$
            ld      a,#SIO1_VEC
sio_prog_vec$:
            out     (c),a
sio_prog_wr4$:
            ld      a,#4
            out     (c),a
            ld      a,SIO_ST_WR4(ix)
            out     (c),a
            ld      a,#3
            out     (c),a
            ld      a,SIO_ST_WR3(ix)
            out     (c),a
            ld      a,#5
            out     (c),a
            ld      a,SIO_ST_WR5(ix)
            out     (c),a
            ld      a,#1
            out     (c),a
            ld      a,#SIO_WR1_DEFAULT
            out     (c),a
            ret

sio_line_from_cfg$:                     ; de=cfg, ix=state -> b=wr3,c=wr4,d=wr5
            ld      a,(de)              ; baud lo (accepted, not clocked yet)
            inc     de
            ld      a,(de)              ; baud hi
            inc     de
            ld      a,(de)              ; data bits
            cp      #5
            jr      z,sio_bits5$
            cp      #6
            jr      z,sio_bits6$
            cp      #7
            jr      z,sio_bits7$
            cp      #8
            jr      z,sio_bits8$
            scf
            ret
sio_bits5$:
            ld      b,#0x01
            ld      d,#0x8a
            jr      sio_bits_done$
sio_bits6$:
            ld      b,#0x81
            ld      d,#0xca
            jr      sio_bits_done$
sio_bits7$:
            ld      b,#0x41
            ld      d,#0xaa
            jr      sio_bits_done$
sio_bits8$:
            ld      b,#0xc1
            ld      d,#0xea
sio_bits_done$:
            ld      c,#SIO_WR4_DEFAULT
            inc     de
            ld      a,(de)              ; parity
            cp      #SIO_PARITY_NONE
            jr      z,sio_parity_done$
            cp      #SIO_PARITY_EVEN
            jr      z,sio_parity_even$
            cp      #SIO_PARITY_ODD
            jr      z,sio_parity_odd$
            scf
            ret
sio_parity_even$:
            set     0,c
            set     1,c
            jr      sio_parity_done$
sio_parity_odd$:
            set     0,c
sio_parity_done$:
            inc     de
            ld      a,(de)              ; stop bits
            cp      #1
            jr      z,sio_stops_done$
            cp      #2
            jr      nz,sio_line_fail$
            ld      c,#0x4c
sio_stops_done$:
            or      a
            ret
sio_line_fail$:
            scf
            ret

sio_tx_kick$:                           ; ix=state, starts one byte if idle
            ld      c,SIO_ST_CTRL(ix)
            in      a,(c)
            and     #SIO_RR0_TX_EMPTY
            ret     z
            ld      a,SIO_ST_TXCOUNT(ix)
            or      a
            ret     z
            ld      l,SIO_ST_TXBUF(ix)
            ld      h,SIO_ST_TXBUF+1(ix)
            ld      e,SIO_ST_TXHEAD(ix)
            ld      d,#0
            add     hl,de
            ld      a,(hl)
            ld      b,a
            ld      a,SIO_ST_TXHEAD(ix)
            inc     a
            cp      SIO_ST_TXSZ(ix)
            jr      c,stk_head_ok$
            xor     a
stk_head_ok$:
            ld      SIO_ST_TXHEAD(ix),a
            ld      a,SIO_ST_TXCOUNT(ix)
            dec     a
            ld      SIO_ST_TXCOUNT(ix),a
            call    drv_tx_refill
            ld      c,SIO_ST_DATA(ix)
            ld      a,b
            out     (c),a
            ret

sio_service_state$:                     ; ix=state
sss_rx$:
            ld      c,SIO_ST_CTRL(ix)
            in      a,(c)
            and     #SIO_RR0_RX_AVAIL
            jr      z,sss_tx$
            ld      c,SIO_ST_DATA(ix)
            in      a,(c)
            ld      e,a
            ld      a,SIO_ST_RDLEFT(ix)
            or      SIO_ST_RDLEFT+1(ix)
            jr      z,sss_ring$
            ld      l,SIO_ST_RDBUF(ix)
            ld      h,SIO_ST_RDBUF+1(ix)
            ld      a,h
            or      l
            jr      z,sss_rx$
            ld      a,e
            ld      (hl),a
            inc     hl
            ld      SIO_ST_RDBUF(ix),l
            ld      SIO_ST_RDBUF+1(ix),h
            ld      l,SIO_ST_RDLEFT(ix)
            ld      h,SIO_ST_RDLEFT+1(ix)
            dec     hl
            ld      SIO_ST_RDLEFT(ix),l
            ld      SIO_ST_RDLEFT+1(ix),h
            ld      a,h
            or      l
            call    z,drv_complete_read_busy_ix
            jr      sss_rx$
sss_ring$:
            ld      a,SIO_ST_RXCOUNT(ix)
            cp      SIO_ST_RXSZ(ix)
            jr      nc,sss_overrun$
            ld      l,SIO_ST_RXBUF(ix)
            ld      h,SIO_ST_RXBUF+1(ix)
            ld      a,h
            or      l
            jr      z,sss_rx$
            ld      b,e                 ; keep received byte while tail is used
            ld      d,#0
            ld      a,SIO_ST_RXTAIL(ix)
            ld      e,a
            add     hl,de
            ld      a,b
            ld      (hl),a
            ld      a,SIO_ST_RXTAIL(ix)
            inc     a
            cp      SIO_ST_RXSZ(ix)
            jr      c,sss_tail_ok$
            xor     a
sss_tail_ok$:
            ld      SIO_ST_RXTAIL(ix),a
            ld      a,SIO_ST_RXCOUNT(ix)
            inc     a
            ld      SIO_ST_RXCOUNT(ix),a
            jr      sss_rx$
sss_overrun$:
            call    drv_set_error_ix
            jr      sss_rx$

sss_tx$:
            ld      c,SIO_ST_CTRL(ix)
            in      a,(c)
            and     #SIO_RR0_TX_EMPTY
            ret     z
            ld      a,SIO_ST_TXCOUNT(ix)
            or      a
            jr      z,sss_done_tx$
            call    sio_tx_kick$
            call    drv_update_busy_ix
            ret
sss_done_tx$:
            ld      a,SIO_ST_WRACT(ix)
            or      a
            ret     z
            ld      a,SIO_ST_WRLEFT(ix)
            or      SIO_ST_WRLEFT+1(ix)
            ret     nz
            jp      drv_complete_write_busy_ix

            ;; ----------------------------------------------------------------
            ;; _sio0_isr() / _sio1_isr()
            ;; ----------------------------------------------------------------
            ;; shared irq skeleton: preserve all cpu state, service both
            ;; channels on the chip, issue "return from interrupt" through
            ;; channel A and then retire with z80 reti.
            ;; ----------------------------------------------------------------
_sio0_isr::
            call    drv_isr_enter
            ld      ix,#sio_state0$
            call    sio_service_state$
            ld      ix,#sio_state1$
            call    sio_service_state$
            ld      c,#SIO0A_CTRL_PORT
            ld      a,#SIO_WRCMD_RTI
            out     (c),a
            jp      drv_isr_exit

_sio1_isr::
            call    drv_isr_enter
            ld      ix,#sio_state2$
            call    sio_service_state$
            ld      ix,#sio_state3$
            call    sio_service_state$
            ld      c,#SIO1A_CTRL_PORT
            ld      a,#SIO_WRCMD_RTI
            out     (c),a
            jp      drv_isr_exit

            ;; ----------------------------------------------------------------
            ;; _sio_purge_owner(<de> owner)
            ;; ----------------------------------------------------------------
            ;; owner-death cleanup drops the logical lock and abandons any still
            ;; pending read/write request that belongs to `owner`. queued-but-not-
            ;; yet-sent tx bytes are discarded so no stale buffer pointer survives
            ;; a dead owner.
            ;; ----------------------------------------------------------------
_sio_purge_owner::
            ld      ix,#sio_state0$
            call    sio_purge_state$
            ld      ix,#sio_state1$
            call    sio_purge_state$
            ld      ix,#sio_state2$
            call    sio_purge_state$
            ld      ix,#sio_state3$
            jr      sio_purge_state$

sio_purge_state$:
            call    drv_purge_owner_ix
            jp      drv_update_busy_ix

sio_dev_drv::
            .dw     pio_dev_drv
            .dw     0x0000
            .dw     sio_init
            .dw     sio_open
            .dw     drv_close_nop
            .dw     sio_read
            .dw     sio_write
            .dw     sio_ioctl

sio_dev0::
sio_dev0$:
            .dw     0x0000
            .db     't','t','y','S','0',0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     sio_dev_drv

sio_dev1::
sio_dev1$:
            .dw     0x0000
            .db     't','t','y','S','1',0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     sio_dev_drv

sio_dev2::
sio_dev2$:
            .dw     0x0000
            .db     't','t','y','S','2',0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     sio_dev_drv

sio_dev3::
sio_dev3$:
            .dw     0x0000
            .db     't','t','y','S','3',0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     sio_dev_drv

            ;; uninitialized driver scratch belongs in _SYSVARS (BSS); a .ds in
            ;; _INITIALIZED reserves space the linker does not account for against
            ;; the following area, so the per-channel clear can overrun the
            ;; _INITIALIZED/_SYSVARS boundary into kernel state (e.g. ir_refcnt).
            .area   _SYSVARS

sio_state0$:
            .ds     SIO_ST_SIZE
sio_state1$:
            .ds     SIO_ST_SIZE
sio_state2$:
            .ds     SIO_ST_SIZE
sio_state3$:
            .ds     SIO_ST_SIZE

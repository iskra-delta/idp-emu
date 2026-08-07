            ;; pio.s
            ;;
            ;; partner mainboard z80 pio stream driver
            ;;
            ;; each PIO port is exposed as one async byte-stream style device:
            ;;
            ;;   - input mode latches one hardware byte per PIO interrupt, then
            ;;     copies it directly into a waiting read request or into a
            ;;     software RX ring
            ;;   - output mode stages bytes in a software TX queue and pushes the
            ;;     next byte only after the peripheral acknowledge interrupt
            ;;   - one pending read request and one pending write request are
            ;;     supported per port; lock ioctls let one logical owner reserve
            ;;     the device and avoid multi-process request queueing
            ;;
            ;; public ioctls mirror the SIO ABI:
            ;;
            ;;   0x20 set rx-ring / tx-queue sizes before first open
            ;;   0x21 lock to current owner
            ;;   0x22 unlock from current owner
            ;;   0x23 switch port mode through a tiny binary struct
            ;;
            ;; 2026-06-17   tstih
            .module pio

            .include "dev.inc"
            .include "drv.inc"
            .include "pio.inc"

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
            .globl  pio_init
            .globl  pio_dev0
            .globl  _ir_set
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  ir_refcnt
            .globl  _owner_cleanup_register
            .globl  _thread_current
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  __sys_heap
            .globl  _pioa_isr
            .globl  _piob_isr
            .globl  _pio_purge_owner
            .globl  rtc_dev_drv

            .equ    THREAD_PROCESS,      22

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; pio_open(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; allocate heap-backed rx/tx buffers on first use, program the
            ;; selected mode and mark the instance open. if another owner holds
            ;; the logical lock, open fails.
            ;; ----------------------------------------------------------------
pio_open::
            call    pio_owner_guard$
            jp      c,drv_err
            call    drv_alloc_buffers_ix
            jp      c,drv_err
            call    _ir_disable
            call    pio_program_mode$
            ld      l,PIO_ST_DEV(ix)
            ld      h,PIO_ST_DEV+1(ix)
            ld      de,#DEV_FLAGS
            add     hl,de
            set     0,(hl)
            res     2,(hl)
            call    _ir_enable
            ld      hl,#DRV_OK
            ret

pio_init_dev0$:
            ld      a,#PIOA_DATA_PORT
            ld      b,#PIOA_CTRL_PORT
            ld      de,#pio_state0$
            jr      pio_init_dev$

pio_init_dev1$:
            ld      a,#PIOB_DATA_PORT
            ld      b,#PIOB_CTRL_PORT
            ld      de,#pio_state1$

pio_init_dev$:                          ; hl=dev, a=data, b=ctrl, de=state
            push    hl
            push    de
            ld      bc,#DEV_DATA
            add     hl,bc
            ld      (hl),a
            inc     hl
            ld      (hl),b
            inc     hl
            pop     de
            ld      (hl),e
            inc     hl
            ld      (hl),d
            pop     hl

            push    hl
            push    de
            ex      de,hl
            xor     a
            ld      b,#PIO_ST_SIZE
pid_zero$:
            ld      (hl),a
            inc     hl
            djnz    pid_zero$
            pop     de
            pop     hl
            push    de
            pop     ix
            ld      PIO_ST_DEV(ix),l
            ld      PIO_ST_DEV+1(ix),h
            ld      bc,#DEV_DATA
            add     hl,bc
            ld      a,(hl)
            ld      PIO_ST_DATA(ix),a
            inc     hl
            ld      a,(hl)
            ld      PIO_ST_CTRL(ix),a
            ld      a,#PIO_DEFAULT_RX
            ld      PIO_ST_RXSZ(ix),a
            ld      a,#PIO_DEFAULT_TX
            ld      PIO_ST_TXSZ(ix),a
            xor     a
            ld      PIO_ST_MODE(ix),a    ; default = output
            ret

            ;; ----------------------------------------------------------------
            ;; pio_init()
            ;; ----------------------------------------------------------------
            ;; install one IM2 vector per PIO port, register the owner-death
            ;; cleanup hook and seed the current vectors into the hardware.
            ;; mode programming is deferred to open / SETMODE.
            ;; ----------------------------------------------------------------
pio_init::
            ld      hl,#pio_dev0$
            call    pio_init_dev0$
            ld      hl,#pio_dev1$
            call    pio_init_dev1$
            ld      a,#PIOA_VEC
            ld      de,#_pioa_isr
            call    _ir_set
            ld      a,#PIOB_VEC
            ld      de,#_piob_isr
            call    _ir_set
            ld      hl,#_pio_purge_owner
            call    _owner_cleanup_register
            ld      c,#PIOA_CTRL_PORT
            ld      a,#PIOA_VEC
            out     (c),a
            ld      c,#PIOB_CTRL_PORT
            ld      a,#PIOB_VEC
            out     (c),a
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= pio_read (<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; only valid in input mode. drains the software RX ring first, then
            ;; stores the remainder as the single pending read request.
            ;; ----------------------------------------------------------------
pio_read::
            call    drv_zero_ok_bc_ix
            ret     z
            push    de                  ; save buffer (owner guard clobbers DE)
            push    ix                  ; stacked event for drv_stream_read_ix
            call    pio_owner_guard$
            jr      c,pior_fail$
            ld      a,PIO_ST_MODE(ix)
            cp      #PIO_MODE_INPUT
            jr      nz,pior_fail$
            ld      hl,#2               ; DE = buffer (below the event on the stack)
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            call    drv_stream_read_ix  ; consumes the stacked event; DE=buf, ix=state
            pop     hl                  ; drop the saved-buffer slot
            ret     nc
            jp      drv_err
pior_fail$:
            pop     ix                  ; drop event
            pop     de                  ; drop buffer
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= pio_write(<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; only valid in output mode. copies bytes into the software tx queue
            ;; and writes the first byte immediately when no hardware transfer is
            ;; already waiting for its peripheral acknowledge edge.
            ;; ----------------------------------------------------------------
pio_write::
            call    drv_zero_ok_bc_ix
            ret     z
            push    ix
            call    pio_owner_guard$
            jr      c,piow_fail$
            ld      a,PIO_ST_MODE(ix)
            cp      #PIO_MODE_OUTPUT
            jr      nz,piow_fail$
            call    drv_stream_write_ix
            jr      c,piow_fail_pop$
            call    pio_tx_start$
            call    drv_update_busy_ix
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
piow_fail_pop$:
            pop     ix
piow_fail$:
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= pio_ioctl(<hl> dev, <de> params, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; 0x20 set buffer sizes before first open/allocation
            ;; 0x21 lock to current owner
            ;; 0x22 unlock from current owner
            ;; 0x23 set input/output mode from pio_modecfg_t
            ;; ----------------------------------------------------------------
pio_ioctl::
            ld      a,b
            or      a
            jp      nz,drv_err
            call    pio_state_by_dev$
            ld      a,c
            cp      #PIO_IOCTL_LOCK
            jp      z,drv_lock_ix
            cp      #PIO_IOCTL_UNLOCK
            jp      z,drv_unlock_ix
            call    drv_owner_guard_ix
            jp      c,drv_err
            ld      a,c
            cp      #PIO_IOCTL_SETBUFS
            jp      z,drv_setbufs_ix
            cp      #PIO_IOCTL_SETMODE
            jr      z,pio_setmode$
            jp      drv_err

pio_setmode$:
            ld      a,(de)
            cp      #2
            jp      nc,drv_err
            call    _ir_disable
            ld      a,PIO_ST_RDLEFT(ix)
            or      PIO_ST_RDLEFT+1(ix)
            jr      nz,pio_setmode_busy$
            ld      a,PIO_ST_WRACT(ix)
            or      a
            jr      nz,pio_setmode_busy$
            ld      a,PIO_ST_TXCOUNT(ix)
            or      a
            jr      nz,pio_setmode_busy$
            ld      a,PIO_ST_RXCOUNT(ix)
            or      a
            jr      nz,pio_setmode_busy$
            ld      a,(de)
            ld      PIO_ST_MODE(ix),a
            ld      a,PIO_ST_DEV(ix)
            ld      h,PIO_ST_DEV+1(ix)
            ld      l,a
            ld      de,#DEV_FLAGS
            add     hl,de
            bit     0,(hl)
            jr      z,pio_setmode_done$
            call    pio_program_mode$
pio_setmode_done$:
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
pio_setmode_busy$:
            call    _ir_enable
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; pio helpers
            ;; ----------------------------------------------------------------
pio_state_by_dev$:                      ; hl=dev -> ix=state, hl preserved
            ld      a,#DEV_DATA + PIO_STATE_PTR_OFF
            jp      drv_state_by_dev_ix_a

pio_owner_guard$:                       ; hl=dev -> cf=1 if locked by other
            call    pio_state_by_dev$
            jp      drv_owner_guard_ix

pio_program_mode$:                      ; ix=state
            ld      c,PIO_ST_CTRL(ix)
            ld      a,#PIOA_VEC
            bit     1,c
            jr      z,ppm_vec$
            ld      a,#PIOB_VEC
ppm_vec$:
            out     (c),a
            ld      a,PIO_ST_MODE(ix)
            or      a
            ld      a,#PIO_CTRL_MODE_OUT
            jr      z,ppm_mode$
            ld      a,#PIO_CTRL_MODE_IN
ppm_mode$:
            out     (c),a
            ld      a,#PIO_CTRL_INT_EN
            out     (c),a
            ret

pio_tx_start$:                          ; ix=state, starts one byte if idle
            ld      a,PIO_ST_TXWAIT(ix)
            or      a
            ret     nz
            ld      a,PIO_ST_TXCOUNT(ix)
            or      a
            ret     z
            ld      l,PIO_ST_TXBUF(ix)
            ld      h,PIO_ST_TXBUF+1(ix)
            ld      e,PIO_ST_TXHEAD(ix)
            ld      d,#0
            add     hl,de
            ld      a,(hl)
            ld      b,a
            ld      a,PIO_ST_TXHEAD(ix)
            inc     a
            cp      PIO_ST_TXSZ(ix)
            jr      c,pts_head_ok$
            xor     a
pts_head_ok$:
            ld      PIO_ST_TXHEAD(ix),a
            ld      a,PIO_ST_TXCOUNT(ix)
            dec     a
            ld      PIO_ST_TXCOUNT(ix),a
            ld      c,PIO_ST_DATA(ix)
            ld      a,b
            out     (c),a
            ld      a,#1
            ld      PIO_ST_TXWAIT(ix),a
            ret

pio_service_state$:                     ; ix=state
            ld      a,PIO_ST_MODE(ix)
            or      a
            jr      z,pss_tx$
            ld      c,PIO_ST_DATA(ix)
            in      a,(c)
            ld      e,a
            ld      a,PIO_ST_RDLEFT(ix)
            or      PIO_ST_RDLEFT+1(ix)
            jr      z,pss_ring$
            ld      l,PIO_ST_RDBUF(ix)
            ld      h,PIO_ST_RDBUF+1(ix)
            ld      a,e
            ld      (hl),a
            inc     hl
            ld      PIO_ST_RDBUF(ix),l
            ld      PIO_ST_RDBUF+1(ix),h
            ld      l,PIO_ST_RDLEFT(ix)
            ld      h,PIO_ST_RDLEFT+1(ix)
            dec     hl
            ld      PIO_ST_RDLEFT(ix),l
            ld      PIO_ST_RDLEFT+1(ix),h
            ld      a,h
            or      l
            call    z,drv_complete_read_busy_ix
            ret
pss_ring$:
            ld      a,PIO_ST_RXCOUNT(ix)
            cp      PIO_ST_RXSZ(ix)
            jr      nc,pss_overrun$
            ld      l,PIO_ST_RXBUF(ix)
            ld      h,PIO_ST_RXBUF+1(ix)
            ld      b,e
            ld      d,#0
            ld      a,PIO_ST_RXTAIL(ix)
            ld      e,a
            add     hl,de
            ld      a,b
            ld      (hl),a
            ld      a,PIO_ST_RXTAIL(ix)
            inc     a
            cp      PIO_ST_RXSZ(ix)
            jr      c,pss_tail_ok$
            xor     a
pss_tail_ok$:
            ld      PIO_ST_RXTAIL(ix),a
            ld      a,PIO_ST_RXCOUNT(ix)
            inc     a
            ld      PIO_ST_RXCOUNT(ix),a
            ret
pss_overrun$:
            jp      drv_set_error_ix

pss_tx$:
            xor     a
            ld      PIO_ST_TXWAIT(ix),a
            call    drv_tx_refill
            ld      a,PIO_ST_TXCOUNT(ix)
            or      a
            jr      z,pss_tx_empty$
            call    pio_tx_start$
            call    drv_update_busy_ix
            ret
pss_tx_empty$:
            ld      a,PIO_ST_WRACT(ix)
            or      a
            jr      z,pss_tx_done$
            ld      a,PIO_ST_WRLEFT(ix)
            or      PIO_ST_WRLEFT+1(ix)
            jp      z,drv_complete_write_busy_ix
pss_tx_done$:
            call    drv_update_busy_ix
            ret

            ;; ----------------------------------------------------------------
            ;; _pioa_isr() / _piob_isr()
            ;; ----------------------------------------------------------------
            ;; preserve full cpu state, service one port, then retire with z80
            ;; reti so the PIO daisy-chain logic can clear the serviced state.
            ;; ----------------------------------------------------------------
_pioa_isr::
            call    drv_isr_enter
            ld      ix,#pio_state0$
            call    pio_service_state$
            jp      drv_isr_exit

_piob_isr::
            call    drv_isr_enter
            ld      ix,#pio_state1$
            call    pio_service_state$
            jp      drv_isr_exit

            ;; ----------------------------------------------------------------
            ;; _pio_purge_owner(<de> owner)
            ;; ----------------------------------------------------------------
            ;; owner-death cleanup drops the logical lock and abandons any still
            ;; pending read/write that belongs to `owner`. already-issued output
            ;; bytes may still finish their hardware acknowledge cycle, but every
            ;; queued software byte is discarded.
            ;; ----------------------------------------------------------------
_pio_purge_owner::
            ld      ix,#pio_state0$
            call    pio_purge_state$
            ld      ix,#pio_state1$
            jr      pio_purge_state$

pio_purge_state$:
            call    drv_purge_owner_ix
            jp      drv_update_busy_ix

pio_dev_drv::
            .dw     rtc_dev_drv
            .dw     0x0000
            .dw     pio_init
            .dw     pio_open
            .dw     drv_close_nop
            .dw     pio_read
            .dw     pio_write
            .dw     pio_ioctl

pio_dev0::
pio_dev0$:
            .dw     pio_dev1$
            .db     'p','i','o','A',0,0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     pio_dev_drv

pio_dev1$:
            .dw     0x0000
            .db     'p','i','o','B',0,0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     pio_dev_drv

            ;; uninitialized scratch -> _SYSVARS (BSS), not _INITIALIZED.
            .area   _SYSVARS

pio_state0$:
            .ds     PIO_ST_SIZE
pio_state1$:
            .ds     PIO_ST_SIZE

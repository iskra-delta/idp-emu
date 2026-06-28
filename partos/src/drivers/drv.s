            ;; drv.s
            ;;
            ;; common bios driver helpers
            ;;
            ;; 2026-06-13   tstih
            .module drv

            .include "../partos.inc"
            .include "dev.inc"
            .include "drv.inc"

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
            .globl  drv_err
            .globl  drv_complete_write_ix
            .globl  drv_complete_read_ix
            .globl  drv_complete_write_busy_ix
            .globl  drv_complete_read_busy_ix
            .globl  drv_purge_owner_ix
            .globl  drv_lock_ix
            .globl  drv_unlock_ix
            .globl  drv_setbufs_ix
            .globl  _evt_set
            .globl  _thread_current
            .globl  _thread_first_waiting
            .globl  __thread_robin
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  __sys_heap
            .globl  ir_refcnt

            .equ    THREAD_WAIT, 16
            .equ    THREAD_PROCESS, 22
            .equ    EVENT_STATE, 4
            .equ    SIGNALED, 1

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; drv_isr_enter()
            ;; drv_isr_exit()
            ;; ----------------------------------------------------------------
            ;; shared full-register interrupt wrapper for driver ISRs. callers
            ;; enter with `call drv_isr_enter`; the helper saves every register,
            ;; increments ir_refcnt, recovers the call's return address from
            ;; the buried stack slot and jumps to the ISR body placed directly
            ;; after that call. the body must finish with `jp drv_isr_exit`.
            ;; ----------------------------------------------------------------
drv_isr_enter::
            di
            xor     a
            ld      (drv_need_resched$),a
            inc     a
            ld      (drv_in_isr$),a
            ;; switch off the interrupted thread's stack onto the shared
            ;; top-of-common-RAM ISR stack BEFORE saving anything. the thread's
            ;; stack only holds the hardware interrupt frame; all ISR working
            ;; state lives on the system stack so a deep ISR can never overflow
            ;; into and corrupt the thread it interrupted. `ld (nn),sp` and
            ;; `ld sp,nn` touch no registers, so the thread's regs are intact.
            ld      (drv_thread_sp$),sp
            ld      sp,#DRV_ISR_STACK_TOP
            push    af
            push    bc
            push    de
            push    hl
            push    ix
            push    iy
            ex      af,af'
            push    af
            ex      af,af'
            exx
            push    bc
            push    de
            push    hl
            exx
            ld      hl,#ir_refcnt
            inc     (hl)
            ;; recover this call's return address (the ISR body) from the top of
            ;; the interrupted thread's stack, and bump the saved thread SP past
            ;; it so drv_isr_exit's reti pops the hardware-pushed interrupt PC.
            ld      hl,(drv_thread_sp$)
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            inc     hl
            ld      (drv_thread_sp$),hl
            ex      de,hl
            jp      (hl)

drv_isr_exit::
            ld      hl,#ir_refcnt
            dec     (hl)
            exx
            pop     hl
            pop     de
            pop     bc
            exx
            ex      af,af'
            pop     af
            ex      af,af'
            pop     iy
            pop     ix
            pop     hl
            pop     de
            pop     bc
            pop     af
            ;; back onto the interrupted thread's stack (now pointing at the
            ;; hardware interrupt frame), then return into the thread.
            ld      sp,(drv_thread_sp$)
            xor     a
            ld      (drv_in_isr$),a
            ld      a,(drv_need_resched$)
            or      a
            jr      z,drv_isr_reti$
            ld      (drv_need_resched$),a ; preserve non-zero until the jump
            xor     a
            ld      (drv_need_resched$),a
            jp      __thread_robin
drv_isr_reti$:
            ei
            reti

            .area   _SYSVARS
drv_thread_sp$:
            .ds     2
drv_in_isr$:
            .ds     1
drv_need_resched$:
            .ds     1
            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; drv_signal_done(<ix> *event)
            ;; ----------------------------------------------------------------
            ;; marks an async-completion event signaled. a null event is a
            ;; no-op (fire-and-forget). clobbers a, de, hl (and flags).
            ;; ----------------------------------------------------------------
drv_signal_done::
            push    ix
            pop     hl                  ; hl = event
            ld      a,h
            or      l
            ret     z                   ; no event -> nothing to signal
            ld      a,#EV_SIGNALED
            push    af
            inc     sp                  ; leave A (signaled) as the 1-byte arg
            call    _evt_set            ; must CALL: evt_set reads its 1-byte arg
            ld      a,(drv_in_isr$)
            or      a
            jr      z,drv_signal_done_ret$
            call    drv_waiting_has_signaled$
            or      a
            jr      z,drv_signal_done_ret$
            ld      a,#1
            ld      (drv_need_resched$),a
drv_signal_done_ret$:
            ret                         ; above the return addr; a tail jp misaligns

            ;; ----------------------------------------------------------------
            ;; <a> <= drv_waiting_has_signaled$()
            ;; ----------------------------------------------------------------
            ;; returns 1 when any thread on the waiting list currently has a
            ;; signaled event, otherwise 0. this lets driver ISRs avoid a blind
            ;; round-robin reschedule when the interrupted thread can simply
            ;; continue and consume its now-signaled completion event itself.
            ;; ----------------------------------------------------------------
drv_waiting_has_signaled$:
            ld      hl,(_thread_first_waiting)
dwhs_thread$:
            ld      a,h
            or      l
            jr      z,dwhs_no$
            push    hl                  ; save current waiting thread
            ld      de,#THREAD_WAIT
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = wait array
            inc     hl
            ld      b,(hl)              ; b = num_events
            ld      a,b
            or      a
            jr      z,dwhs_next$
dwhs_evt$:
            ld      a,(de)
            ld      l,a
            inc     de
            ld      a,(de)
            ld      h,a
            inc     de                  ; hl = event, de = next array slot
            push    de
            ld      de,#EVENT_STATE
            add     hl,de
            ld      a,(hl)
            pop     de
            cp      #SIGNALED
            jr      z,dwhs_yes$
            djnz    dwhs_evt$
dwhs_next$:
            pop     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            jr      dwhs_thread$
dwhs_yes$:
            pop     hl
            ld      a,#1
            ret
dwhs_no$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; drv_zero_ok_bc_ix()
            ;; ----------------------------------------------------------------
            ;; shared read/write fast-path: when bc == 0, signal ix and return
            ;; DRV_OK with z set so callers can `ret z`.
            ;; ----------------------------------------------------------------
drv_zero_ok_bc_ix::
            ld      a,b
            or      c
            ret     nz
            call    drv_signal_done
            ld      hl,#DRV_OK
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; drv_owner_current() -> <de> owner
            ;; ----------------------------------------------------------------
            ;; shared logical-owner lookup for stream-style drivers:
            ;;   1. current thread's process, if any
            ;;   2. otherwise the current thread itself
            ;;   3. otherwise NONE (0) when no thread is active
            ;; ----------------------------------------------------------------
drv_owner_current::
            ld      hl,(_thread_current)
            ld      d,h
            ld      e,l
            ld      a,d
            or      e
            ret     z
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            ret     nz
            ld      de,(_thread_current)
            ret

            ;; ----------------------------------------------------------------
            ;; drv_state_by_dev_ix_a(<hl> dev, <a> dev-data offset) -> <ix> state
            ;; ----------------------------------------------------------------
            ;; resolves a driver-private state pointer stored in dev.data[].
            ;; hl is preserved so wrappers can chain further checks cheaply.
            ;; ----------------------------------------------------------------
drv_state_by_dev_ix_a::
            push    hl
            ld      e,a
            ld      d,#0
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            push    de
            pop     ix
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; drv_owner_guard_ix() -> cf=1 if ix->lock belongs to someone else
            ;; ----------------------------------------------------------------
drv_owner_guard_ix::
            ld      a,DRV_ST_LOCK(ix)
            or      DRV_ST_LOCK+1(ix)
            ret     z
            push    hl
            call    drv_owner_current
            pop     hl
            ld      a,e
            cp      DRV_ST_LOCK(ix)
            jr      nz,dog_fail$
            ld      a,d
            cp      DRV_ST_LOCK+1(ix)
            jr      nz,dog_fail$
            or      a
            ret
dog_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; drv_alloc_buffers_ix() -> cf=1 on allocation failure
            ;; ----------------------------------------------------------------
            ;; shared lazy rx/tx ring allocation for the sio/pio stream-state
            ;; prefix. the owning dev_t* is used as the heap owner.
            ;; ----------------------------------------------------------------
drv_alloc_buffers_ix::
            ld      a,DRV_ST_RXBUF(ix)
            or      DRV_ST_RXBUF+1(ix)
            jr      nz,dab_tx$
            ld      l,DRV_ST_DEV(ix)
            ld      h,DRV_ST_DEV+1(ix)
            push    hl
            ld      d,#0x00
            ld      e,DRV_ST_RXSZ(ix)
            ld      hl,#__sys_heap
            call    _mem_allocate
            pop     bc
            ld      a,d
            or      e
            jr      z,dab_fail$
            ld      DRV_ST_RXBUF(ix),e
            ld      DRV_ST_RXBUF+1(ix),d
dab_tx$:
            ld      a,DRV_ST_TXBUF(ix)
            or      DRV_ST_TXBUF+1(ix)
            jr      nz,dab_ok$
            ld      l,DRV_ST_DEV(ix)
            ld      h,DRV_ST_DEV+1(ix)
            push    hl
            ld      d,#0x00
            ld      e,DRV_ST_TXSZ(ix)
            ld      hl,#__sys_heap
            call    _mem_allocate
            pop     bc
            ld      a,d
            or      e
            jr      nz,dab_tx_ok$
            ld      e,DRV_ST_RXBUF(ix)
            ld      d,DRV_ST_RXBUF+1(ix)
            ld      hl,#__sys_heap
            call    _mem_free
            xor     a
            ld      DRV_ST_RXBUF(ix),a
            ld      DRV_ST_RXBUF+1(ix),a
            jr      dab_fail$
dab_tx_ok$:
            ld      DRV_ST_TXBUF(ix),e
            ld      DRV_ST_TXBUF+1(ix),d
dab_ok$:
            or      a
            ret
dab_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; drv_read_drain_ring(<ix> state, <de> buf, <bc> left)
            ;; ----------------------------------------------------------------
            ;; copies already-buffered rx bytes into one pending read buffer.
            ;; returns with updated de/bc and the rx ring consumed as far as it
            ;; can go.
            ;; ----------------------------------------------------------------
drv_read_drain_ring::
            ld      a,DRV_ST_RXCOUNT(ix)
            or      a
            ret     z
drdr_loop$:
            ld      a,b
            or      c
            ret     z
            ld      a,DRV_ST_RXCOUNT(ix)
            or      a
            ret     z
            ld      l,DRV_ST_RXBUF(ix)
            ld      h,DRV_ST_RXBUF+1(ix)
            push    de
            ld      e,DRV_ST_RXHEAD(ix)
            ld      d,#0
            add     hl,de
            ld      a,(hl)
            pop     de
            ld      (de),a
            inc     de
            dec     bc
            ld      a,DRV_ST_RXHEAD(ix)
            inc     a
            cp      DRV_ST_RXSZ(ix)
            jr      c,drdr_head_ok$
            xor     a
drdr_head_ok$:
            ld      DRV_ST_RXHEAD(ix),a
            ld      a,DRV_ST_RXCOUNT(ix)
            dec     a
            ld      DRV_ST_RXCOUNT(ix),a
            jr      drdr_loop$

            ;; ----------------------------------------------------------------
            ;; drv_tx_refill(<ix> state)
            ;; ----------------------------------------------------------------
            ;; copies bytes from one pending write buffer into the software tx
            ;; queue until either source bytes or queue space run out.
            ;; ----------------------------------------------------------------
drv_tx_refill::
            ld      a,DRV_ST_TXCOUNT(ix)
            cp      DRV_ST_TXSZ(ix)
            ret     nc
dtrf_loop$:
            ld      a,DRV_ST_WRLEFT(ix)
            or      DRV_ST_WRLEFT+1(ix)
            ret     z
            ld      a,DRV_ST_TXCOUNT(ix)
            cp      DRV_ST_TXSZ(ix)
            ret     nc
            ld      l,DRV_ST_WRBUF(ix)
            ld      h,DRV_ST_WRBUF+1(ix)
            ld      a,(hl)
            inc     hl
            ld      DRV_ST_WRBUF(ix),l
            ld      DRV_ST_WRBUF+1(ix),h
            ld      l,DRV_ST_TXBUF(ix)
            ld      h,DRV_ST_TXBUF+1(ix)
            ld      e,DRV_ST_TXTAIL(ix)
            ld      d,#0
            add     hl,de
            ld      (hl),a
            ld      a,DRV_ST_TXTAIL(ix)
            inc     a
            cp      DRV_ST_TXSZ(ix)
            jr      c,dtrf_tail_ok$
            xor     a
dtrf_tail_ok$:
            ld      DRV_ST_TXTAIL(ix),a
            ld      a,DRV_ST_TXCOUNT(ix)
            inc     a
            ld      DRV_ST_TXCOUNT(ix),a
            ld      l,DRV_ST_WRLEFT(ix)
            ld      h,DRV_ST_WRLEFT+1(ix)
            dec     hl
            ld      DRV_ST_WRLEFT(ix),l
            ld      DRV_ST_WRLEFT+1(ix),h
            jr      dtrf_loop$

            ;; ----------------------------------------------------------------
            ;; drv_stream_read_ix(<ix> state, <de> buf, <bc> count)
            ;; ----------------------------------------------------------------
            ;; shared "accept one async read" path for stream drivers that use
            ;; the common state prefix. the caller must already have:
            ;;   - pushed ix so the saved event pointer sits on the stack
            ;;   - resolved ix to the driver-private state
            ;;   - performed any driver-specific mode/ownership checks
            ;;
            ;; success consumes the stacked event pointer. failure leaves it on
            ;; the stack so the caller can pop it in its own error path.
            ;; ----------------------------------------------------------------
drv_stream_read_ix::
            call    _ir_disable
            call    drv_read_drain_ring
            ld      a,b
            or      c
            jr      nz,dsri_need_wait$
            call    _ir_enable
            pop     ix
            call    drv_signal_done
            ld      hl,#DRV_OK
            or      a
            ret
dsri_need_wait$:
            ld      a,DRV_ST_RDLEFT(ix)
            or      DRV_ST_RDLEFT+1(ix)
            jr      z,dsri_store$
            call    _ir_enable
            scf
            ld      hl,#DRV_ERR
            ret
dsri_store$:
            ld      DRV_ST_RDBUF(ix),e
            ld      DRV_ST_RDBUF+1(ix),d
            ld      DRV_ST_RDLEFT(ix),c
            ld      DRV_ST_RDLEFT+1(ix),b
            call    drv_owner_current
            ld      DRV_ST_RDOWNER(ix),e
            ld      DRV_ST_RDOWNER+1(ix),d
            pop     de
            ld      DRV_ST_RDEVT(ix),e
            ld      DRV_ST_RDEVT+1(ix),d
            call    drv_update_busy_ix
            call    _ir_enable
            ld      hl,#DRV_OK
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; drv_stream_write_ix(<ix> state, <de> buf, <bc> count)
            ;; ----------------------------------------------------------------
            ;; shared "accept one async write" prefix for stream drivers with
            ;; the common state layout. on success the caller still needs to
            ;; kick hardware-specific transmission before re-enabling IRQs.
            ;;
            ;; success consumes the stacked event pointer and leaves interrupts
            ;; disabled so the caller can do the device-specific start step.
            ;; failure leaves the stacked event pointer untouched.
            ;; ----------------------------------------------------------------
drv_stream_write_ix::
            call    _ir_disable
            ld      a,DRV_ST_WRACT(ix)
            or      a
            jr      z,dswi_store$
            call    _ir_enable
            scf
            ld      hl,#DRV_ERR
            ret
dswi_store$:
            ld      DRV_ST_WRBUF(ix),e
            ld      DRV_ST_WRBUF+1(ix),d
            ld      DRV_ST_WRLEFT(ix),c
            ld      DRV_ST_WRLEFT+1(ix),b
            call    drv_owner_current
            ld      DRV_ST_WROWNER(ix),e
            ld      DRV_ST_WROWNER+1(ix),d
            pop     de
            ld      DRV_ST_WREVT(ix),e
            ld      DRV_ST_WREVT+1(ix),d
            ld      a,#1
            ld      DRV_ST_WRACT(ix),a
            call    drv_tx_refill
            ld      hl,#DRV_OK
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; drv_update_busy_ix(<ix> state)
            ;; drv_update_lock_ix(<ix> state)
            ;; drv_set_error_ix(<ix> state)
            ;; ----------------------------------------------------------------
            ;; shared dev->flags helpers for the stream-state prefix.
            ;; ----------------------------------------------------------------
drv_update_busy_ix::
            push    de
            ld      l,DRV_ST_DEV(ix)
            ld      h,DRV_ST_DEV+1(ix)
            ld      de,#DEV_FLAGS
            add     hl,de
            ld      a,DRV_ST_RDLEFT(ix)
            or      DRV_ST_RDLEFT+1(ix)
            jr      nz,dubi_set$
            ld      a,DRV_ST_WRACT(ix)
            or      a
            jr      nz,dubi_set$
            ld      a,DRV_ST_TXCOUNT(ix)
            or      a
            jr      nz,dubi_set$
            res     1,(hl)
            pop     de
            ret
dubi_set$:
            set     1,(hl)
            pop     de
            ret

drv_update_lock_ix::
            push    de
            ld      l,DRV_ST_DEV(ix)
            ld      h,DRV_ST_DEV+1(ix)
            ld      de,#DEV_FLAGS
            add     hl,de
            ld      a,DRV_ST_LOCK(ix)
            or      DRV_ST_LOCK+1(ix)
            jr      z,dul_clear$
            set     4,(hl)
            pop     de
            ret
dul_clear$:
            res     4,(hl)
            pop     de
            ret

drv_set_error_ix::
            push    de
            ld      l,DRV_ST_DEV(ix)
            ld      h,DRV_ST_DEV+1(ix)
            ld      de,#DEV_FLAGS
            add     hl,de
            set     2,(hl)
            pop     de
            ret

drv_zero_span_hl_b$:
            xor     a
dzsb_loop$:
            ld      (hl),a
            inc     hl
            djnz    dzsb_loop$
            ret

            ;; ----------------------------------------------------------------
            ;; drv_complete_write_ix() / drv_complete_read_ix()
            ;; ----------------------------------------------------------------
            ;; shared completion tails for the common stream-state prefix.
            ;; they clear the pending request fields, signal the stored event
            ;; if present and preserve ix for the caller.
            ;; ----------------------------------------------------------------
drv_complete_write_ix::
            push    ix
            ld      l,DRV_ST_WREVT(ix)
            ld      h,DRV_ST_WREVT+1(ix)
            push    hl
            push    ix
            pop     hl
            ld      de,#DRV_ST_WRBUF
            add     hl,de
            ld      b,#9
            call    drv_zero_span_hl_b$
            pop     hl
            push    hl
            pop     ix
            call    drv_signal_done
            pop     ix
            ret

drv_complete_read_ix::
            push    ix
            ld      l,DRV_ST_RDEVT(ix)
            ld      h,DRV_ST_RDEVT+1(ix)
            push    hl
            push    ix
            pop     hl
            ld      de,#DRV_ST_RDBUF
            add     hl,de
            ld      b,#8
            call    drv_zero_span_hl_b$
            pop     hl
            push    hl
            pop     ix
            call    drv_signal_done
            pop     ix
            ret

drv_complete_write_busy_ix::
            call    drv_complete_write_ix
            jp      drv_update_busy_ix

drv_complete_read_busy_ix::
            call    drv_complete_read_ix
            jp      drv_update_busy_ix

            ;; ----------------------------------------------------------------
            ;; drv_purge_owner_ix(<de> owner)
            ;; ----------------------------------------------------------------
            ;; shared owner-death cleanup for the common stream-state prefix.
            ;; logical lock, pending read and pending write are dropped only
            ;; when they belong to the supplied owner.
            ;; ----------------------------------------------------------------
drv_purge_owner_ix::
            push    de
            ld      a,e
            cp      DRV_ST_LOCK(ix)
            jr      nz,dpoi_read$
            ld      a,d
            cp      DRV_ST_LOCK+1(ix)
            jr      nz,dpoi_read$
            xor     a
            ld      DRV_ST_LOCK(ix),a
            ld      DRV_ST_LOCK+1(ix),a
            push    de
            call    drv_update_lock_ix
            pop     de
dpoi_read$:
            pop     de
            push    de
            ld      a,e
            cp      DRV_ST_RDOWNER(ix)
            jr      nz,dpoi_write$
            ld      a,d
            cp      DRV_ST_RDOWNER+1(ix)
            jr      nz,dpoi_write$
            push    ix
            pop     hl
            ld      de,#DRV_ST_RDBUF
            add     hl,de
            ld      b,#8
            call    drv_zero_span_hl_b$
dpoi_write$:
            pop     de
            push    de
            ld      a,e
            cp      DRV_ST_WROWNER(ix)
            jr      nz,dpoi_done$
            ld      a,d
            cp      DRV_ST_WROWNER+1(ix)
            jr      nz,dpoi_done$
            push    ix
            pop     hl
            ld      de,#DRV_ST_TXHEAD
            add     hl,de
            ld      b,#3
            call    drv_zero_span_hl_b$
            push    ix
            pop     hl
            ld      de,#DRV_ST_WRBUF
            add     hl,de
            ld      b,#9
            call    drv_zero_span_hl_b$
dpoi_done$:
            pop     de
            ret

            ;; ----------------------------------------------------------------
            ;; drv_lock_ix() / drv_unlock_ix() / drv_setbufs_ix()
            ;; ----------------------------------------------------------------
            ;; shared stream ioctl helpers for the common state prefix.
            ;; lock/unlock resolve the current logical owner and update the
            ;; published dev->flags bit. setbufs accepts a 2-byte config at de:
            ;;   [0] rx ring size
            ;;   [1] tx queue size
            ;; and only succeeds before the real buffers are allocated.
            ;; ----------------------------------------------------------------
drv_lock_ix::
            call    drv_owner_current
            ld      a,d
            or      e
            jr      z,dli_fail$
            call    _ir_disable
            ld      a,DRV_ST_LOCK(ix)
            or      DRV_ST_LOCK+1(ix)
            jr      z,dli_set$
            ld      a,e
            cp      DRV_ST_LOCK(ix)
            jr      nz,dli_fail_irq$
            ld      a,d
            cp      DRV_ST_LOCK+1(ix)
            jr      nz,dli_fail_irq$
            jr      dli_ok$
dli_set$:
            ld      DRV_ST_LOCK(ix),e
            ld      DRV_ST_LOCK+1(ix),d
dli_ok$:
            call    drv_update_lock_ix
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
dli_fail_irq$:
            call    _ir_enable
dli_fail$:
            ld      hl,#DRV_ERR
            ret

drv_unlock_ix::
            call    drv_owner_current
            ld      a,d
            or      e
            jr      z,dui_fail$
            call    _ir_disable
            ld      a,e
            cp      DRV_ST_LOCK(ix)
            jr      nz,dui_fail_irq$
            ld      a,d
            cp      DRV_ST_LOCK+1(ix)
            jr      nz,dui_fail_irq$
            xor     a
            ld      DRV_ST_LOCK(ix),a
            ld      DRV_ST_LOCK+1(ix),a
            call    drv_update_lock_ix
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
dui_fail_irq$:
            call    _ir_enable
dui_fail$:
            ld      hl,#DRV_ERR
            ret

drv_setbufs_ix::
            call    _ir_disable
            ld      a,DRV_ST_RXBUF(ix)
            or      DRV_ST_RXBUF+1(ix)
            jr      nz,dsbi_fail$
            ld      a,DRV_ST_TXBUF(ix)
            or      DRV_ST_TXBUF+1(ix)
            jr      nz,dsbi_fail$
            ld      a,(de)
            or      a
            jr      z,dsbi_fail$
            ld      DRV_ST_RXSZ(ix),a
            inc     de
            ld      a,(de)
            or      a
            jr      z,dsbi_fail$
            ld      DRV_ST_TXSZ(ix),a
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
dsbi_fail$:
            call    _ir_enable
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; drv_reset_dev(<hl> *dev, <de> *driver)
            ;; ----------------------------------------------------------------
            ;; resets the mutable runtime fields of a static device instance:
            ;; next, flags, private data and owning driver pointer. the name
            ;; bytes stay as-is so each driver can keep its static prefix.
            ;;
            ;; input(s):
            ;;  hl  ... dev_t*
            ;;  de  ... dev_drv_t*
            ;; output(s):
            ;;  hl  ... original dev_t*
            ;; destroys:
            ;;  a, bc
            ;; ----------------------------------------------------------------
drv_reset_dev::
            push    hl
            xor     a
            ld      (hl),a              ; next low
            inc     hl
            ld      (hl),a              ; next high
            ld      bc,#DEV_FLAGS-1
            add     hl,bc               ; hl = dev->flags
            ld      (hl),a              ; flags = 0
            inc     hl                  ; hl = dev->data
            ld      b,#DEV_DATA_SIZE
drd_zero$:
            ld      (hl),a
            inc     hl
            djnz    drd_zero$
            ld      (hl),e              ; driver low
            inc     hl
            ld      (hl),d              ; driver high
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; generic stub entry points for unfinished drivers
            ;; ----------------------------------------------------------------
drv_open_ok::
            ld      hl,#DRV_OK
            ret

drv_err::
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; drv_open_pos0(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; resets the shared 24-bit byte cursor in dev.data[] to offset 0.
            ;; drivers that behave like flat files can use this as open().
            ;; ----------------------------------------------------------------
drv_open_pos0::
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            pop     hl
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <de> lba <= drv_prep_rw256(<hl> *dev, <bc> count)
            ;; ----------------------------------------------------------------
            ;; validates a 256-byte aligned flat-file transfer:
            ;;   - count must be a multiple of 256 bytes (c == 0)
            ;;   - current byte offset must also be 256-byte aligned
            ;; on success de receives the current 16-bit block/lba index
            ;; (byte_offset >> 8). zero-length requests are treated as success
            ;; and leave de unspecified.
            ;; ----------------------------------------------------------------
drv_prep_rw256::
            ld      a,c
            or      a
            jr      z,drp_chkpos$
            ld      hl,#DRV_ERR
            ret
drp_chkpos$:
            push    bc
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(hl)
            or      a
            jr      z,drp_load$
            pop     bc
            ld      hl,#DRV_ERR
            ret
drp_load$:
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     bc
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; drv_advance_pos256(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; advances the shared 24-bit byte cursor by one 256-byte block.
            ;; ----------------------------------------------------------------
drv_advance_pos256::
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS1
            add     hl,bc
            inc     (hl)
            jr      nz,dap_done$
            inc     hl
            inc     (hl)
dap_done$:
            pop     hl
            ret

drv_close_nop::
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= drv_ioctl_pos24(<hl> *dev, <de> *pos, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; shared byte-position ioctl used by flat-file style block drivers.
            ;; de points to a 24-bit little-endian byte offset buffer.
            ;; ----------------------------------------------------------------
drv_ioctl_pos24::
            ld      a,b
            or      a
            jr      z,dip_cmd$
            ld      hl,#DRV_ERR
            ret
dip_cmd$:
            ld      a,c
            cp      #IOCTL_GETPOS
            jr      z,dip_get$
            cp      #IOCTL_SETPOS
            jr      z,dip_set$
            ld      hl,#DRV_ERR
            ret
dip_get$:
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            ld      a,(hl)
            ld      (de),a
            pop     hl
            ld      hl,#DRV_OK
            ret
dip_set$:
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(de)
            ld      (hl),a
            inc     hl
            inc     de
            ld      a,(de)
            ld      (hl),a
            inc     hl
            inc     de
            ld      a,(de)
            ld      (hl),a
            pop     hl
            ld      hl,#DRV_OK
            ret

drv_read_unsupported::
drv_write_unsupported::
drv_ioctl_unsupported::
            ld      hl,#DRV_ERR
            ret

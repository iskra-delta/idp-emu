            ;; thread.s
            ;;
            ;; threading: thread objects, the round-robin context switch and the
            ;; cooperative state lists. hand-written replacement for the yos
            ;; thread.c + throbin.s, ported to the partos kernel (lists, sysobj,
            ;; heap and the reference-counted interrupt guard).
            ;;
            ;; threads live on four state lists (suspended / running / waiting /
            ;; terminated); thread_current points at the running one. the vbl
            ;; interrupt drives _thread_robin, which saves the current context,
            ;; chains timers, picks the next runnable thread and restores it.
            ;; a thread blocked in thread_wait4events sits on the waiting list
            ;; and is NOT scheduled until one of its events is signaled.
            ;;
            ;; layout (thread_t, 24 bytes):
            ;;   +0  next        (sysobj)
            ;;   +2  owner       (sysobj)
            ;;   +4  sp          (saved stack pointer)
            ;;   +6  startup[10] (bootstrap code: call entry / ld hl,t / jp exit)
            ;;   +16 wait        (event_t ** or 0)
            ;;   +18 num_events  (count of events in wait[])
            ;;   +19 state       (THREAD_STATE_*)
            ;;   +20 joined      (joined thread list, unused for now)
            ;;   +22 process     (parent process, or 0 for kernel threads)
            ;;
            ;; public c entry points use z80 sdcccall(1); pointer results in de.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module thread

            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_suspend
            .globl  _thread_exit
            .globl  _thread_wait4events
            .globl  __thread_select_next
            .globl  __thread_robin

            .globl  _thread_current
            .globl  _thread_first_suspended
            .globl  _thread_first_running
            .globl  _thread_first_waiting
            .globl  _thread_first_terminated

            .globl  _so_create
            .globl  _so_destroy
            .globl  _list_insert
            .globl  _list_remove
            .globl  _mem_allocate
            .globl  _mem_free_owner
            .globl  __owner_cleanup_run
            .globl  _process_reap
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  __tmr_chain
            .globl  __usr_heap
            .globl  __sys_heap
            .globl  ir_refcnt

            ;; --- thread_t offsets ---
            .equ    THREAD_SP,          4
            .equ    THREAD_STARTUP,     6
            .equ    THREAD_WAIT,        16
            .equ    THREAD_NUM_EVENTS,  18
            .equ    THREAD_STATE,       19
            .equ    THREAD_JOINED,      20
            .equ    THREAD_PROCESS,     22
            .equ    THREAD_SIZE,        24
            ;; context: af,bc,de,hl,ix,iy,af',bc',de',hl' (20) + ret (2) = 22
            .equ    CONTEXT_SIZE,       22

            ;; --- thread states ---
            .equ    THREAD_STATE_SUSPENDED,     0
            .equ    THREAD_STATE_RUNNING,       1
            .equ    THREAD_STATE_WAITING,       2
            .equ    THREAD_STATE_TERMINATED,    4

            ;; --- event ---
            .equ    EVENT_STATE,        4
            .equ    SIGNALED,           1

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _thread_create(<hl> entry, <de> stack_size, <stack> proc)
            ;; ----------------------------------------------------------------
            ;; allocates a thread sysobj on the suspended list plus a private
            ;; stack, primes the stack so the first context restore drops into
            ;; the bootstrap code (call entry; on return ld hl,t; jp thread_exit)
            ;; and returns the new thread (or 0). runs in a critical section, so
            ;; the inputs are stashed in static scratch instead of juggling sp.
            ;; ----------------------------------------------------------------
_thread_create::
            call    _ir_disable
            ld      (tc_entry$),hl
            ld      (tc_stksz$),de
            ld      hl,#2
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (tc_process$),de    ; stacked arg: parent process

            ;; t = so_create(&suspended, THREAD_SIZE, NONE)
            ld      de,#0x0000
            push    de                  ; owner = NONE
            ld      de,#THREAD_SIZE
            ld      hl,#_thread_first_suspended
            call    _so_create
            pop     bc                  ; drop owner
            ld      a,d
            or      e
            jp      z,tc_fail0$
            ld      (tc_t$),de

            ;; stack = mem_allocate(owner ? user_heap : sys_heap, stack_size, owner=t)
            push    de                  ; owner = t
            ld      de,(tc_stksz$)
            ld      hl,(tc_process$)
            ld      a,h
            or      l
            ld      hl,#__sys_heap
            jr      z,tc_heap_ready$
            ld      hl,#__usr_heap
tc_heap_ready$:
            call    _mem_allocate
            pop     bc                  ; drop owner
            ld      a,d
            or      e
            jp      z,tc_fail1$
            ld      (tc_stack$),de

            ;; --- t->wait = 0, num_events = 0, state = SUSPENDED ---
            ld      hl,(tc_t$)
            ld      de,#THREAD_WAIT
            add     hl,de
            xor     a
            ld      (hl),a              ; wait lo
            inc     hl
            ld      (hl),a              ; wait hi
            inc     hl
            ld      (hl),a              ; num_events
            inc     hl
            ld      (hl),a              ; state = SUSPENDED

            ;; --- t->process = process ---
            ld      hl,(tc_t$)
            ld      de,#THREAD_PROCESS
            add     hl,de
            ld      de,(tc_process$)
            ld      (hl),e
            inc     hl
            ld      (hl),d

            ;; --- t->sp = stack + stack_size - CONTEXT_SIZE ---
            ld      de,(tc_stack$)
            ld      hl,(tc_stksz$)
            add     hl,de               ; hl = end = stack + stack_size
            ld      (tc_end$),hl
            ld      de,#CONTEXT_SIZE
            or      a
            sbc     hl,de               ; hl = end - CONTEXT_SIZE
            ex      de,hl               ; de = sp value
            ld      hl,(tc_t$)
            ld      bc,#THREAD_SP
            add     hl,bc               ; &t->sp
            ld      (hl),e
            inc     hl
            ld      (hl),d

            ;; --- top word of the stack = &t->startup (the first ret target) ---
            ld      hl,(tc_end$)
            dec     hl
            dec     hl                  ; ret slot = end - 2
            ld      bc,(tc_t$)
            ld      a,c
            add     a,#THREAD_STARTUP
            ld      e,a
            ld      a,b
            adc     a,#0
            ld      d,a                 ; de = &t->startup
            ld      (hl),e
            inc     hl
            ld      (hl),d

            ;; --- emit bootstrap code into t->startup ---
            ld      hl,(tc_t$)
            ld      de,#THREAD_STARTUP
            add     hl,de               ; hl = &t->startup
            ld      (hl),#0xcd          ; call nn
            inc     hl
            ld      de,(tc_entry$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ld      (hl),#0x21          ; ld hl,nn
            inc     hl
            ld      de,(tc_t$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ld      (hl),#0xc3          ; jp nn
            inc     hl
            ld      de,#_thread_exit
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ld      (hl),#0x00          ; nop (pad)

            ld      de,(tc_t$)          ; return value
            call    _ir_enable
            pop     hl                  ; ret addr
            pop     bc                  ; drop stacked process arg
            jp      (hl)

tc_fail1$:
            ld      de,(tc_t$)
            ld      hl,#_thread_first_suspended
            call    _so_destroy
tc_fail0$:
            ld      de,#0x0000
            call    _ir_enable
            pop     hl                  ; ret addr
            pop     bc                  ; drop stacked process arg
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; thread_move$(<hl> &src, <de> &dst, <bc> t, <a> state)
            ;; ----------------------------------------------------------------
            ;; moves t from the src list to the dst list and sets t->state.
            ;; does nothing if t was not on the src list.
            ;; ----------------------------------------------------------------
thread_move$:
            push    de                  ; &dst
            push    bc                  ; t
            push    af                  ; state
            ld      d,b
            ld      e,c                 ; de = t  (hl = &src)
            call    _list_remove
            ld      a,d
            or      e
            jr      z,tmv_fail$
            pop     af                  ; a = state
            pop     bc                  ; bc = t
            pop     de                  ; de = &dst
            ;; t->state = state
            ld      h,b
            ld      l,c
            push    bc                  ; t
            push    de                  ; &dst
            ld      de,#THREAD_STATE
            add     hl,de
            ld      (hl),a
            pop     hl                  ; hl = &dst
            pop     de                  ; de = t
            jp      _list_insert        ; tail: list_insert(&dst, t)
tmv_fail$:
            pop     af
            pop     bc
            pop     de
            ret

            ;; ----------------------------------------------------------------
            ;; _thread_resume(<hl> t)   suspended -> running
            ;; ----------------------------------------------------------------
_thread_resume::
            ld      b,h
            ld      c,l
            ld      hl,#_thread_first_suspended
            ld      de,#_thread_first_running
            ld      a,#THREAD_STATE_RUNNING
            jp      thread_move$

            ;; ----------------------------------------------------------------
            ;; _thread_suspend(<hl> t)  running -> suspended, then yield
            ;; ----------------------------------------------------------------
_thread_suspend::
            ld      b,h
            ld      c,l
            ld      hl,#_thread_first_running
            ld      de,#_thread_first_suspended
            ld      a,#THREAD_STATE_SUSPENDED
            call    thread_move$
            halt                        ; immediate yield; wakes on resume
            ret

            ;; ----------------------------------------------------------------
            ;; _thread_exit(<hl> t)     running -> terminated, never returns
            ;; ----------------------------------------------------------------
            ;; also the bootstrap return target: when a thread's entry function
            ;; returns, startup does ld hl,t; jp _thread_exit.
            ;; ----------------------------------------------------------------
_thread_exit::
            ld      b,h
            ld      c,l
            ld      hl,#_thread_first_running
            ld      de,#_thread_first_terminated
            ld      a,#THREAD_STATE_TERMINATED
            call    thread_move$
tex_dead$:
            halt                        ; scheduler reaps us; never run again
            jr      tex_dead$

            ;; ----------------------------------------------------------------
            ;; _thread_wait4events(<hl> events, <stack> num_events)
            ;; ----------------------------------------------------------------
            ;; blocks the current thread on a set of events: records the event
            ;; array, moves the thread to the waiting list and yields. the
            ;; scheduler wakes it once any of the events is signaled. the single
            ;; stacked byte argument is cleaned on return.
            ;; ----------------------------------------------------------------
_thread_wait4events::
            push    hl                  ; save events
            ld      hl,#4
            add     hl,sp               ; -> num_events (sp+2 entry, +2 push)
            ld      a,(hl)
            pop     de                  ; de = events
            ld      b,a                 ; b = num_events
            ld      hl,(_thread_current)
            ld      a,h
            or      l
            jr      z,tw_ret$           ; no current thread (shouldn't happen)
            ;; thread_current->wait = events, num_events = b
            push    hl                  ; save t
            ld      a,#THREAD_WAIT
            add     a,l
            ld      l,a
            jr      nc,tw_w$
            inc     h
tw_w$:
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; t->wait = events
            inc     hl
            ld      (hl),b              ; t->num_events = num_events
            pop     hl                  ; hl = t
            ;; fast path: if any event is already signaled, do not block at all
            ;; (keeps immediate-completion / fake-async operations cheap).
            push    hl                  ; save t
            call    thread_any_signaled$  ; hl = t -> a
            pop     bc                  ; bc = t
            or      a
            jr      nz,tw_ret$          ; already signaled: quick return
            ;; move running -> waiting, state = WAITING
            ld      hl,#_thread_first_running
            ld      de,#_thread_first_waiting
            ld      a,#THREAD_STATE_WAITING
            call    thread_move$
            halt                        ; yield; wakes when an event signals
tw_ret$:
            pop     hl                  ; ret addr
            inc     sp                  ; drop 1-byte num_events arg
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; <a> <= thread_any_signaled$(<hl> t)
            ;; ----------------------------------------------------------------
            ;; returns 1 if any of t's wait events is signaled, else 0.
            ;; stack-neutral; clobbers a, bc, de, hl.
            ;; ----------------------------------------------------------------
thread_any_signaled$:
            ld      de,#THREAD_WAIT
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = wait array
            inc     hl
            ld      b,(hl)              ; b = num_events
            ld      a,b
            or      a
            jr      z,tas_no$
tas_loop$:
            ld      a,(de)
            ld      l,a
            inc     de
            ld      a,(de)
            ld      h,a
            inc     de                  ; hl = event, de = array cursor
            push    de
            ld      de,#EVENT_STATE
            add     hl,de
            ld      a,(hl)              ; a = event->state
            pop     de
            cp      #SIGNALED
            jr      z,tas_yes$
            djnz    tas_loop$
tas_no$:
            xor     a
            ret
tas_yes$:
            ld      a,#1
            ret

            ;; ----------------------------------------------------------------
            ;; thread_cleanup_terminated$()
            ;; ----------------------------------------------------------------
            ;; reaps terminated threads (except the one currently running):
            ;; frees the thread's owned memory (its stack) and the thread sysobj.
            ;; ----------------------------------------------------------------
thread_cleanup_terminated$:
            ld      hl,(_thread_first_terminated)
tct_loop$:
            ld      a,h
            or      l
            ret     z
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            dec     hl                  ; de = next, hl = t
            push    de                  ; next
            push    hl                  ; t
            ld      de,(_thread_current)
            ld      a,h
            cp      d
            jr      nz,tct_reap$
            ld      a,l
            cp      e
            jr      z,tct_skip$         ; t == thread_current -> cannot reap
tct_reap$:
            ;; give optional subsystems a chance to unlink objects that still
            ;; point at this owner before the heap walk frees owned blocks.
            pop     hl
            ld      (tct_saved_t$),hl
            ld      de,#THREAD_PROCESS
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (tct_process$),de   ; remember parent process before free
            ld      hl,(tct_saved_t$)
            push    hl                  ; keep t alive for the real reap steps
            ex      de,hl               ; de = dead owner
            call    __owner_cleanup_run
            pop     hl
            push    hl                  ; restore saved t for the next step
tct_have_t$:
            ;; free any thread-owned stack regardless of which heap supplied it
            ex      de,hl               ; de = t
            push    de
            ld      hl,#__sys_heap
            call    _mem_free_owner
            pop     de
            push    de
            ld      hl,#__usr_heap
            call    _mem_free_owner
            ;; so_destroy(&terminated, t) frees the thread sysobj
            pop     bc
            push    bc                  ; t (keep)
            ld      e,c
            ld      d,b
            ld      hl,#_thread_first_terminated
            call    _so_destroy
            pop     bc                  ; drop t
            ld      hl,(tct_process$)
            ld      a,h
            or      l
            call    nz,_process_reap
            pop     hl                  ; hl = next
            jr      tct_loop$
tct_skip$:
            pop     hl                  ; drop t
            pop     hl                  ; hl = next
            jr      tct_loop$

            ;; ----------------------------------------------------------------
            ;; <de> <= __thread_select_next()
            ;; ----------------------------------------------------------------
            ;; reaps terminated threads, wakes any waiting thread whose event is
            ;; signaled, then returns the next thread to run (round-robin over
            ;; the running list).
            ;; ----------------------------------------------------------------
__thread_select_next::
            call    thread_cleanup_terminated$
            ld      hl,(_thread_first_waiting)
tsn_wl$:
            ld      a,h
            or      l
            jr      z,tsn_select$
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            dec     hl                  ; de = next, hl = t
            push    de                  ; next
            push    hl                  ; t
            call    thread_any_signaled$
            or      a
            jr      z,tsn_nosig$
            ;; signaled: t = top-of-stack, move waiting -> running, state RUNNING
            pop     bc                  ; bc = t
            ld      h,b
            ld      l,c
            push    bc                  ; t
            ld      de,#THREAD_STATE
            add     hl,de
            ld      (hl),#THREAD_STATE_RUNNING
            pop     bc                  ; t
            push    bc                  ; t
            ld      e,c
            ld      d,b
            ld      hl,#_thread_first_waiting
            call    _list_remove
            pop     bc                  ; t
            push    bc                  ; t
            ld      e,c
            ld      d,b
            ld      hl,#_thread_first_running
            call    _list_insert
            pop     bc                  ; drop t
            pop     hl                  ; hl = next
            jr      tsn_wl$
tsn_nosig$:
            pop     bc                  ; drop t
            pop     hl                  ; hl = next
            jr      tsn_wl$
tsn_select$:
            ld      hl,(_thread_current)
            ld      a,h
            or      l
            jr      z,tsn_first$        ; no current -> first running
            push    hl
            ld      de,#THREAD_STATE
            add     hl,de
            ld      a,(hl)
            pop     hl
            cp      #THREAD_STATE_RUNNING
            jr      nz,tsn_first$       ; current no longer running
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = current->next
            ld      a,d
            or      e
            jr      z,tsn_first$        ; end of running list -> wrap
            ret                         ; de = current->next
tsn_first$:
            ld      de,(_thread_first_running)
            ret

            ;; ----------------------------------------------------------------
            ;; __thread_robin()  -- im 2 vbl scheduler tick / context switch
            ;; ----------------------------------------------------------------
            ;; preemptive round-robin. saves the current thread's full context,
            ;; chains timers, selects the next thread and restores it. holds the
            ;; ir bracket across the timer chain and list work so nested ir_enable
            ;; cannot re-enable interrupts before the final ei; reti. install in
            ;; place of ir_vbl_handler at the vbl vector once threading is up.
            ;; ----------------------------------------------------------------
__thread_robin::
            di
            push    af
            push    hl
            ld      hl,#ir_refcnt
            inc     (hl)                ; hold the bracket (no nested ei)
            ld      hl,(_thread_current)
            ld      a,h
            or      l
            jr      z,trbn_no_current$
            ;; save current context (af, hl already pushed)
            push    bc
            push    de
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
            ;; thread_current->sp = sp
            ld      hl,(_thread_current)
            inc     hl
            inc     hl
            inc     hl
            inc     hl                  ; -> ->sp
            ld      de,#0x0000
            ex      de,hl               ; de = &sp, hl = 0
            add     hl,sp               ; hl = sp
            ex      de,hl               ; hl = &sp, de = sp
            ld      (hl),e
            inc     hl
            ld      (hl),d
            jr      trbn_exec$
trbn_no_current$:
            pop     hl                  ; balance the early af/hl pushes
            pop     af
trbn_exec$:
            call    __tmr_chain
            call    __thread_select_next  ; de = next | 0
            ld      hl,#ir_refcnt
            dec     (hl)                ; release bracket (no ei yet)
            ld      a,d
            or      e
            jr      nz,trbn_have_next$
            ld      de,(_thread_current)  ; nothing new: stay on current
            ld      a,d
            or      e
            jr      z,trbn_done$
trbn_have_next$:
            ex      de,hl
            ld      (_thread_current),hl
            inc     hl
            inc     hl
            inc     hl
            inc     hl                  ; -> ->sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ld      sp,hl               ; switch to next thread's stack
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
            pop     de
            pop     bc
            pop     hl
            pop     af
trbn_done$:
            ei
            reti

            .area   _INITIALIZED

_thread_current::
            .dw     0x0000
_thread_first_suspended::
            .dw     0x0000
_thread_first_running::
            .dw     0x0000
_thread_first_waiting::
            .dw     0x0000
_thread_first_terminated::
            .dw     0x0000

            .area   _SYSVARS

            ;; thread_create scratch (valid only inside its critical section)
tc_entry$:
            .ds     2
tc_stksz$:
            .ds     2
tc_process$:
            .ds     2
tc_t$:
            .ds     2
tc_stack$:
            .ds     2
tc_end$:
            .ds     2
tct_saved_t$:
            .ds     2
tct_process$:
            .ds     2

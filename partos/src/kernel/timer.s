            ;; timer.s
            ;;
            ;; soft timer hooks. each timer is a system object carrying a hook
            ;; routine and a tick reload value; the timer chain is walked once
            ;; per scheduler tick (50 hz) and fires hooks whose countdown has
            ;; expired. hand-written replacement for yos timer.c, smaller than
            ;; the compiled C.
            ;;
            ;; layout (timer_t):
            ;;   +0  next        (sysobj, list link)
            ;;   +2  owner       (sysobj)
            ;;   +4  hook        (void (*)(void))
            ;;   +6  ticks       (reload value)
            ;;   +8  _tick_count  (internal countdown)
            ;;
            ;; a timer with ticks == N fires every N+1 ticks; ticks == 0
            ;; (EVERYTIME) fires on every tick.
            ;;
            ;; public c entry points use z80 sdcccall(1); 16-bit/pointer
            ;; results are returned in de:
            ;;
            ;;   _tmr_install:   in  hl = hook, de = ticks, stack = owner
            ;;                   out de = timer or 0
            ;;   _tmr_uninstall: in  hl = timer
            ;;                   out de = freed timer or 0
            ;;   __tmr_chain:    in  - (void), out -; runs inside the guarded
            ;;                   50 hz interrupt, so no di/ei is needed here.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module timer

            .globl  _tmr_install
            .globl  _tmr_uninstall
            .globl  __tmr_chain
            .globl  __tmr_first
            .globl  _so_create
            .globl  _so_destroy
            .globl  _list_iterate

            .equ    TIMER_HOOK,         4
            .equ    TIMER_TICKS,        6
            .equ    TIMER_TICK_COUNT,   8
            .equ    TIMER_SIZE,         10

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _tmr_install(<hl> hook, <de> ticks, <stack> owner)
            ;; ----------------------------------------------------------------
            ;; allocates a timer sysobj, fills hook/ticks/_tick_count and links
            ;; it into the timer list. the 2-byte stacked owner argument is
            ;; cleaned on return (callee-clean).
            ;; ----------------------------------------------------------------
_tmr_install::
            push    hl                  ; save hook
            push    de                  ; save ticks
            ld      hl,#6
            add     hl,sp               ; -> owner (entry sp+2, +4 for pushes)
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = owner
            push    de                  ; stack owner for so_create
            ld      de,#TIMER_SIZE
            ld      hl,#__tmr_first
            call    _so_create          ; de = timer or 0; owner left on stack
            pop     bc                  ; discard owner arg
            ld      a,d
            or      e
            jr      z,ti_fail$

            pop     bc                  ; bc = ticks
            pop     hl                  ; hl = hook
            push    de                  ; save timer for return
            ld      a,e
            add     a,#TIMER_HOOK
            ld      e,a
            ld      a,d
            adc     a,#0
            ld      d,a                 ; de = timer + 4 (hook field)
            ld      a,l
            ld      (de),a
            inc     de
            ld      a,h
            ld      (de),a              ; hook stored
            inc     de
            ld      a,c
            ld      (de),a
            inc     de
            ld      a,b
            ld      (de),a              ; ticks stored
            inc     de
            ld      a,c
            ld      (de),a
            inc     de
            ld      a,b
            ld      (de),a              ; _tick_count = ticks
            pop     de                  ; de = timer (return value)
            jr      ti_ret$

ti_fail$:
            pop     bc                  ; discard saved ticks
            pop     bc                  ; discard saved hook
            ;; de = 0
ti_ret$:
            pop     hl                  ; return address
            pop     bc                  ; drop 2-byte owner arg
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; <de> <= _tmr_uninstall(<hl> timer)
            ;; ----------------------------------------------------------------
_tmr_uninstall::
            ex      de,hl               ; de = timer
            ld      hl,#__tmr_first
            jp      _so_destroy

            ;; ----------------------------------------------------------------
            ;; __tmr_chain()
            ;; ----------------------------------------------------------------
            ;; fires all due timers by driving the shared list_iterate over the
            ;; timer list with the per-timer tmr_tick$ callback (so the list
            ;; traversal lives in one place).
            ;;
            ;; precondition: called from inside the 50 hz interrupt, which MUST
            ;; already hold an ir_disable/ir_enable bracket (refcount >= 1).
            ;; that keeps the di/ei inside list_iterate from ever reaching 0,
            ;; so interrupts are never re-enabled mid-isr. this is the same
            ;; contract the C _tmr_chain relied on ("no extra di/ei needed").
            ;; ----------------------------------------------------------------
__tmr_chain::
            ld      hl,(__tmr_first)    ; first element
            ld      de,#tmr_tick$       ; per-element callback
            ld      bc,#0x0000          ; arg (unused)
            push    bc                  ; arg is a stacked, caller-clean param
            call    _list_iterate
            pop     bc                  ; clean arg
            ret

            ;; ----------------------------------------------------------------
            ;; tmr_tick$(<hl> timer, <de> arg)
            ;; ----------------------------------------------------------------
            ;; list_iterate callback. if the timer countdown is zero, reload it
            ;; from ticks and fire the hook; otherwise decrement it. returns to
            ;; list_iterate (firing tail-calls the hook, whose ret continues the
            ;; walk).
            ;; ----------------------------------------------------------------
tmr_tick$:
            push    hl                  ; save timer base (hook may clobber)
            ld      bc,#TIMER_TICK_COUNT
            add     hl,bc               ; hl -> _tick_count
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = _tick_count, hl -> +9
            ld      a,d
            or      e
            jr      z,tt_fire$

            ;; non-zero: store _tick_count - 1
            dec     de
            ld      (hl),d
            dec     hl
            ld      (hl),e
            pop     hl                  ; balance stack
            ret

tt_fire$:
            pop     hl                  ; hl = timer base
            push    hl                  ; keep timer base across reload
            ld      bc,#TIMER_TICKS
            add     hl,bc               ; hl -> ticks
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = ticks, hl -> +7
            inc     hl                  ; hl -> +8 (_tick_count)
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; _tick_count = ticks
            pop     hl                  ; hl = timer base
            ld      bc,#TIMER_HOOK
            add     hl,bc               ; hl -> hook
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = hook routine
            jp      (hl)                ; tail-call hook(); its ret resumes walk

            .area   _INITIALIZED

            ;; head of the timer list
__tmr_first::
            .dw     0x0000

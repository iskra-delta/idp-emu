            ;; ir_vbl_handler.s
            ;;
            ;; im 2 interrupt handler for the vertical-blank (vbl) tick. the
            ;; partner avdc vertical-blank line clocks ctc channel 3, which
            ;; raises this interrupt at the frame rate (~50 hz). the handler
            ;; runs the registered timers via the timer chain.
            ;;
            ;; install with: ir_set(CTC_VEC_VBL, ir_vbl_handler)  (vector 0x8e).
            ;;
            ;; correctness notes:
            ;;  - it saves the FULL register context (main + ix/iy + shadow set)
            ;;    because the interrupted code may use any register and the timer
            ;;    hooks called from the chain are arbitrary code.
            ;;  - it holds the ir bracket (ir_refcnt >= 1) across the chain so
            ;;    the di/ei inside list_iterate (used by _tmr_chain) cannot
            ;;    re-enable interrupts before we are done. it bumps/*drops* the
            ;;    counter directly instead of ir_disable/ir_enable so the final
            ;;    enable is the canonical `ei` immediately before `reti`.
            ;;  - it ends with ei + reti: reti is required for the z80 daisy
            ;;    chain so the ctc clears its interrupt-under-service. the ctc
            ;;    channel auto-reloads, so no port write is needed to ack.
            ;;
            ;; 2026-06-16   tstih
            .module ir_vbl_handler

            .globl  _ir_vbl_handler
            .globl  __tmr_chain
            .globl  ir_refcnt

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; _ir_vbl_handler()  -- im 2 vbl interrupt entry
            ;; ----------------------------------------------------------------
_ir_vbl_handler::
            ;; --- save full register context ---
            push    af
            push    bc
            push    de
            push    hl
            push    ix
            push    iy
            ex      af,af'
            push    af                  ; shadow af
            ex      af,af'
            exx
            push    bc                  ; shadow bc
            push    de                  ; shadow de
            push    hl                  ; shadow hl
            exx

            ;; --- hold the ir bracket (no ei) across the timer chain ---
            ld      hl,#ir_refcnt
            inc     (hl)                ; refcnt++ : keep interrupts off
            call    __tmr_chain         ; run all due timers
            ld      hl,#ir_refcnt
            dec     (hl)                ; refcnt-- : back to entry level

            ;; --- restore full register context (reverse order) ---
            exx
            pop     hl                  ; shadow hl
            pop     de                  ; shadow de
            pop     bc                  ; shadow bc
            exx
            ex      af,af'
            pop     af                  ; shadow af
            ex      af,af'
            pop     iy
            pop     ix
            pop     hl
            pop     de
            pop     bc
            pop     af

            ei
            reti

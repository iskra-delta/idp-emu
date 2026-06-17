            ;; owner_cleanup.s
            ;;
            ;; tiny owner-death cleanup hook used by the thread reaper.
            ;;
            ;; multiple subsystems may need to purge owner-bound state when an
            ;; owner goes away (fat requests, tty locks, pending async tty i/o,
            ;; ...). keeping that as a small callback list lets the kernel stay
            ;; generic enough without pulling os- or driver-specific knowledge
            ;; into thread.s directly.
            ;;
            ;; callback ABI:
            ;;   in : de = dead owner pointer
            ;;   out: none
            ;;   ret: normal z80 ret
            ;;
            ;; public entry points:
            ;;   _owner_cleanup_register(<hl> fn)
            ;;   __owner_cleanup_run(<de> owner)
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module owner_cleanup

            .globl  _owner_cleanup_register
            .globl  __owner_cleanup_run

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; _owner_cleanup_register(<hl> fn)
            ;; ----------------------------------------------------------------
            ;; registers one owner-death cleanup callback. duplicate
            ;; registrations are ignored. hl = 0 clears the whole callback
            ;; table, which is only useful for very early bring-up/reset code.
            ;; ----------------------------------------------------------------
_owner_cleanup_register::
            ld      a,h
            or      l
            jr      nz,ocr_add$
            xor     a
            ld      hl,#owner_cleanup_fn$
            ld      b,#(OWNER_CLEANUP_MAX * 2)
ocr_clear$:
            ld      (hl),a
            inc     hl
            djnz    ocr_clear$
            ret
ocr_add$:
            ld      (owner_cleanup_newfn$),hl
            ld      bc,#0x0000          ; first free slot, if any
            ld      de,#owner_cleanup_fn$
            ld      a,#OWNER_CLEANUP_MAX
ocr_scan$:
            push    af
            ld      a,(de)
            ld      l,a
            inc     de
            ld      a,(de)
            ld      h,a
            dec     de
            ld      a,h
            or      l
            jr      z,ocr_free$
            ld      a,(owner_cleanup_newfn$)
            cp      l
            jr      nz,ocr_next$
            ld      a,(owner_cleanup_newfn$ + 1)
            cp      h
            jr      z,ocr_done$
ocr_next$:
            inc     de
            inc     de
            pop     af
            dec     a
            jr      nz,ocr_scan$
            ret                         ; table full -> ignore
ocr_free$:
            ld      a,b
            or      c
            jr      nz,ocr_keep_free$
            ex      de,hl               ; hl = free slot
            ld      b,h
            ld      c,l                 ; bc = first free slot
            ex      de,hl               ; de = scan ptr
ocr_keep_free$:
            inc     de
            inc     de
            pop     af
            dec     a
            jr      nz,ocr_scan$
            ld      a,b
            or      c
            ret     z
            ld      h,b
            ld      l,c
            ld      a,(owner_cleanup_newfn$)
            ld      (hl),a
            inc     hl
            ld      a,(owner_cleanup_newfn$ + 1)
            ld      (hl),a
ocr_done$:
            ret

            ;; ----------------------------------------------------------------
            ;; __owner_cleanup_run(<de> owner)
            ;; ----------------------------------------------------------------
            ;; runs every registered callback with de = owner. this helper stays
            ;; internal to assembly code, so it uses a tiny custom abi instead
            ;; of an sdcc wrapper path.
            ;; ----------------------------------------------------------------
__owner_cleanup_run::
            ld      hl,#owner_cleanup_fn$
            ld      a,#OWNER_CLEANUP_MAX
ocr_run$:
            push    af
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            inc     hl
            ld      a,b
            or      c
            jr      z,ocr_skip$
            push    hl
            ld      h,b
            ld      l,c
            ld      bc,#ocr_return$
            push    bc
            jp      (hl)
ocr_return$:
            pop     hl
ocr_skip$:
            pop     af
            dec     a
            jr      nz,ocr_run$
            ret

            .area   _INITIALIZED

            .equ    OWNER_CLEANUP_MAX,  4

owner_cleanup_fn$:
            .ds     OWNER_CLEANUP_MAX * 2

owner_cleanup_newfn$:
            .dw     0x0000

            ;; ir.s
            ;;
            ;; interrupt routines
            ;;
            ;; 2023-09-16   tstih
            .module ir

            .globl  _ir_init
            .globl  _ir_disable
            .globl  _ir_enable

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; ir_init()
            ;; ----------------------------------------------------------------
            ;; initialize interrupt routines
            ;;
            ;; destroys:
            ;;  a   ... 0
            ;;  flags
            ;; ----------------------------------------------------------------
_ir_init::
            xor     a
            ld      (ir_refcnt),a
            ret

            ;; ----------------------------------------------------------------
            ;; ir_disable()
            ;; ----------------------------------------------------------------
            ;; execute di instruction with reference counting
            ;;
            ;; destroys:
            ;;  flags
            ;; ----------------------------------------------------------------
_ir_disable::
            di
            push    hl
            ld      hl,#ir_refcnt
            inc     (hl)
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; ir_enable()
            ;; ----------------------------------------------------------------
            ;; execute ei instruction with reference counting
            ;; ----------------------------------------------------------------
_ir_enable::
            di                          ; make sure no one bothers our logic
            push    af                  ; store af
            ld      a,(ir_refcnt)       ; get reference counter
            or      a                   ; set flags
            jr      z,ire_ei$           ; if a==0 then just ei
            dec     a                   ; if a<>0 then dec a
            ld      (ir_refcnt),a       ; write back to counter
            or      a                   ; and check for ei
            jr      nz,ire_done$        ; not yet...
ire_ei$:
            ei
ire_done$:
            pop     af
            ret

            .area   _SYSVARS
            ;; ir reference count
ir_refcnt::
            .ds     1

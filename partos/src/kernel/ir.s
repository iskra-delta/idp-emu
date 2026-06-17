            ;; ir.s
            ;;
            ;; interrupt routines
            ;;
            ;; 2023-09-16   tstih
            .module ir

            .include "../partos.inc"

            .globl  _ir_init
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; ir_init()
            ;; ----------------------------------------------------------------
            ;; initialize interrupt routines
            ;;
            ;; also points the cpu vector base (i register) at the kernel im 2
            ;; table page. im 2 is NOT selected here and ei is left off: only
            ;; the vector page is wired so a later `im 2` + ei can light up
            ;; interrupts atomically once the handlers exist.
            ;;
            ;; destroys:
            ;;  a   ... im 2 page
            ;;  flags
            ;; ----------------------------------------------------------------
_ir_init::
            xor     a
            ld      (ir_refcnt),a
            ld      a,#(KERNEL_IM2_BASE >> 8)
            ld      i,a
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

            ;; ----------------------------------------------------------------
            ;; <de> handler <= _ir_set(<a> vector, <de> handler)
            ;; ----------------------------------------------------------------
            ;; installs a handler into the im 2 vector table and returns the
            ;; previous one. the vector number is the raw byte a z80 peripheral
            ;; puts on the bus during interrupt acknowledge; the cpu reads the
            ;; 2-byte handler address from (i << 8) | vector, so the slot lives
            ;; at KERNEL_IM2_BASE + vector. the table is page-aligned, so the
            ;; vector is simply the low address byte.
            ;;
            ;; input(s):
            ;;  a   ... vector number (0..0xff)
            ;;  de  ... new handler
            ;; output(s):
            ;;  de  ... previous handler
            ;; destroys:
            ;;  a, bc, hl, flags
            ;; ----------------------------------------------------------------
_ir_set::
            push    de                  ; save new handler
            call    _ir_disable         ; guard the table update (a preserved)
            ld      h,#(KERNEL_IM2_BASE >> 8)
            ld      l,a                 ; hl = KERNEL_IM2_BASE + vector
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = previous handler
            pop     bc                  ; bc = new handler
            ld      (hl),b              ; store high byte
            dec     hl
            ld      (hl),c              ; store low byte
            call    _ir_enable          ; de preserved across enable
            ret

            .area   _SYSVARS
            ;; ir reference count
ir_refcnt::
            .ds     1

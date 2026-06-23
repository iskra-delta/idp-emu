            ;; ir.s
            ;;
            ;; interrupt routines
            ;;
            ;; 2023-09-16   tstih
            .module ir

            .include "../partos.inc"

            .globl  __ir_init
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set
            .globl  ir_refcnt
            .globl  ir_armed

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; __ir_init(<hl> im2_table_base)
            ;; ----------------------------------------------------------------
            ;; initialize interrupt routines and point the cpu vector base (i
            ;; register) at the im 2 table page. the table lives in the OS-owned
            ;; shared region (0xc000..0xffff), so its base is NOT a kernel
            ;; constant: the caller (the cold-start, reading the payload's
            ;; declared kernel layout) supplies it. the table must be page
            ;; aligned -- only the high byte of hl is used.
            ;;
            ;; im 2 is NOT selected here and ei is left off: only the vector page
            ;; is wired so a later `im 2` + ei can light up interrupts atomically
            ;; once the handlers exist. from here on _ir_set reads the page back
            ;; from the i register, so no module hardcodes the table address.
            ;;
            ;; input(s):
            ;;  hl  ... im 2 table base (page-aligned)
            ;; destroys:
            ;;  a   ... im 2 page
            ;;  flags
            ;; ----------------------------------------------------------------
__ir_init::
            xor     a
            ld      (ir_armed),a        ; interrupts stay HARD-off until armed
            inc     a
            ld      (ir_refcnt),a       ; refcnt = 1: boot starts inside a disable
                                        ; bracket (the `di` at __sys_kernel). the
                                        ; caller need not _ir_disable again; the
                                        ; first _ir_enable balances this.
            ld      a,h                 ; im 2 page = high byte of the base
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
            jr      z,ire_maybe_ei$     ; if a==0 then maybe ei
            dec     a                   ; if a<>0 then dec a
            ld      (ir_refcnt),a       ; write back to counter
            or      a                   ; and check for ei
            jr      nz,ire_done$        ; not yet...
ire_maybe_ei$:
            ;; only actually enable interrupts once the kernel has armed them
            ;; (after device init). this makes early init immune to a corrupted
            ;; ir_refcnt: device probing can never run with interrupts on.
            ld      a,(ir_armed)
            or      a
            jr      z,ire_done$
            pop     af
            ei
            ret
ire_done$:
            pop     af
            ret

            ;; ----------------------------------------------------------------
            ;; <de> handler <= _ir_set(<a> vector, <de> handler)
            ;; ----------------------------------------------------------------
            ;; installs a handler into the im 2 vector table and returns the
            ;; previous one. the vector number is the raw byte a z80 peripheral
            ;; puts on the bus during interrupt acknowledge; the cpu reads the
            ;; 2-byte handler address from (i << 8) | vector. the table page is
            ;; whatever __ir_init programmed into the i register, so we read it
            ;; back with `ld a,i` rather than hardcode an OS-owned base. the
            ;; table is page-aligned, so the vector is the low address byte.
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
            ld      l,a                 ; l = vector = low byte of the slot
            ld      a,i                 ; a = im 2 page (from __ir_init)
            ld      h,a                 ; hl = (i << 8) | vector
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
            ;; 0 = interrupts kept off regardless of refcnt (early init);
            ;; set to 1 once the scheduler/driver interrupt flow is ready.
ir_armed::
            .ds     1

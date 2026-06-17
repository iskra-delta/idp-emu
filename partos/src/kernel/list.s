            ;; list.s
            ;;
            ;; intrusive singly linked-list helpers shared by the kernel.
            ;; every list-able object starts with a 16-bit next pointer at
            ;; offset 0, so the head cell and each element's next field are
            ;; layout-compatible.
            ;;
            ;; internal helper calling convention:
            ;;
            ;;   _list_match_eq:
            ;;     in : hl = item, de = arg
            ;;     out: a = 0 / 1
            ;;
            ;;   __list_find$:
            ;;     in : hl = first, de = &prev, iy = match, bc = arg
            ;;     out: hl = found item or 0, *prev updated
            ;;
            ;;   __list_iterate$:
            ;;     in : hl = first, iy = fn, bc = arg
            ;;     out: none
            ;;
            ;;   _list_insert / _list_remove:
            ;;     in : hl = &first, de = element
            ;;     out: hl = element on success, 0 on failure
            ;;
            ;;   _list_append:
            ;;     in : hl = &first, de = element or pre-linked chain head
            ;;
            ;; callback calling convention used by list_find/list_iterate:
            ;;   in : hl = item, de = arg
            ;;   out: a = 0 / 1 for match callbacks
            ;;
            ;; public c entry points:
            ;;   _list_* use z80 sdcccall(1)
            ;;   16-bit/pointer results are returned in de there
            ;; ----------------------------------------------------------------
            ;; 2026-06-14   tstih
            .module list

            .globl  _list_match_eq
            .globl  _list_find
            .globl  _list_iterate
            .globl  _list_append
            .globl  _list_insert
            .globl  _list_remove
            .globl  _ir_disable
            .globl  _ir_enable
            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; indirect callback trampoline
            ;; ----------------------------------------------------------------
list_call_iy$:
            push    iy
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= _list_match_eq(<hl> item, <de> arg)
            ;; ----------------------------------------------------------------
_list_match_eq::
            ld      a,l
            cp      e
            jr      nz,lme_false$
            ld      a,h
            cp      d
            jr      nz,lme_false$
            ld      a,#1
            ret
lme_false$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> <= __list_find$(<hl> first, <de> *prev, <iy> match, <bc> arg)
            ;; ----------------------------------------------------------------
            ;; the callback receives hl=item, de=arg and returns non-zero in a
            ;; on match. *prev is cleared before the walk and then updated with
            ;; the previous element pointer on each step.
            ;; ----------------------------------------------------------------
__list_find$:
            xor     a
            ld      (de),a
            inc     de
            ld      (de),a
            dec     de

lf_loop$:
            ld      a,h
            or      l
            jr      z,lf_fail$

            push    de
            push    hl
            push    iy
            push    bc                  ; copy arg to main de
            pop     de
            call    list_call_iy$
            pop     iy
            pop     hl
            pop     de
            or      a
            jr      nz,lf_done$

            ld      a,l
            ld      (de),a
            inc     de
            ld      a,h
            ld      (de),a
            dec     de

            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      lf_loop$

lf_fail$:
            ld      hl,#0x0000
lf_done$:
            ret

_list_find::
            call    _ir_disable
            pop     af
            pop     iy
            pop     bc
            push    bc
            push    iy
            push    af
            call    __list_find$
            ex      de,hl
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; __list_iterate$(<hl> first, <iy> fn, <bc> arg)
            ;; ----------------------------------------------------------------
            ;; visits each element until the list ends.
            ;; ----------------------------------------------------------------
__list_iterate$:
li_loop$:
            ld      a,h
            or      l
            ret     z

            push    bc
            push    hl
            push    iy
            push    bc                  ; copy arg to main de
            pop     de
            call    list_call_iy$
            pop     iy
            pop     hl
            pop     bc

            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      li_loop$

_list_iterate::
            call    _ir_disable
            pop     af
            pop     bc
            push    bc
            push    af
            push    de
            pop     iy
            call    __list_iterate$
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _list_insert(<hl> **first, <de> *el)
            ;; ----------------------------------------------------------------
_list_insert::
            call    _ir_disable
            ld      a,d
            or      e
            jr      z,lii_fail$

            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            dec     hl

            push    hl
            ex      de,hl
            ld      (hl),c
            inc     hl
            ld      (hl),b
            dec     hl
            ex      de,hl
            pop     hl

            ld      (hl),e
            inc     hl
            ld      (hl),d
            call    _ir_enable
            ret

lii_fail$:
            ld      de,#0x0000
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _list_remove(<hl> **first, <de> *el)
            ;; ----------------------------------------------------------------
            ;; walks the list as a chain of pointer cells: &first, then each
            ;; element's next field at offset 0. once the cell containing el is
            ;; found, it is rewired to el->next.
            ;; ----------------------------------------------------------------
_list_remove::
            call    _ir_disable
            ld      a,d
            or      e
            jr      z,lrm_fail$

lrm_walk$:
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            dec     hl
            ld      a,b
            or      c
            jr      z,lrm_fail$

            ld      a,c
            cp      e
            jr      nz,lrm_next$
            ld      a,b
            cp      d
            jr      z,lrm_found$

lrm_next$:
            ld      h,b
            ld      l,c
            jr      lrm_walk$

lrm_found$:
            push    hl                  ; save containing cell
            ld      h,b
            ld      l,c                 ; hl = el
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = el->next
            pop     hl                  ; hl = containing cell
            ld      (hl),c
            inc     hl
            ld      (hl),b
            call    _ir_enable
            ret

lrm_fail$:
            ld      de,#0x0000
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _list_append(<hl> **first, <de> *chain)
            ;; ----------------------------------------------------------------
            ;; appends the supplied element or pre-linked chain to the tail of
            ;; the list and returns the appended head in hl.
            ;; ----------------------------------------------------------------
_list_append::
            call    _ir_disable
            ld      a,d
            or      e
            jr      z,lap_fail$

lap_walk$:
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            dec     hl
            ld      a,b
            or      c
            jr      z,lap_link$
            ld      h,b
            ld      l,c
            jr      lap_walk$

lap_link$:
            ld      (hl),e
            inc     hl
            ld      (hl),d
            call    _ir_enable
            ret

lap_fail$:
            ld      de,#0x0000
            call    _ir_enable
            ret

            ;; list.s
            ;;
            ;; generic singly linked list routines. a list member is any
            ;; structure whose FIRST member is the next pointer. the list
            ;; head variable and a member's next field are then uniform
            ;; cells and the same code handles every list in the system.
            ;;
            ;; 2026-06-11   tstih
            .module list

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; append_list(<hl> **list, <de> *chain)
            ;; ----------------------------------------------------------------
            ;; appends a chain (one or more members linked by their first
            ;; word, zero terminated) to the end of a list. list is the
            ;; address of the head pointer; appending to an empty list sets
            ;; the head. a single member is a chain of one: its next word
            ;; must be zero.
            ;;
            ;; input(s):
            ;;  hl  ... address of the list head pointer
            ;;  de  ... chain to append (may be 0 = no-op)
            ;; destroys:
            ;;  a, bc, hl
            ;;  flags
            ;; ----------------------------------------------------------------
append_list::
            ld      a,d
            or      e
            ret     z                   ; empty chain: nothing to do
al_walk$:
            ld      c,(hl)              ; bc = cell value
            inc     hl
            ld      b,(hl)
            dec     hl                  ; hl = cell address again
            ld      a,b
            or      c
            jr      z,al_link$          ; cell empty: end of list
            ld      h,b
            ld      l,c                 ; follow link (next is at offset 0)
            jr      al_walk$
al_link$:
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; cell = chain
            ret

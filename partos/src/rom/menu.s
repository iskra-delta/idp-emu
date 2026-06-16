            ;; menu.s
            ;;
            ;; tiny rom-side menu engine for BIOS setup screens.
            ;;
            ;; entries are data-driven:
            ;;   kind 0 = highlighted title
            ;;   kind 1 = selectable choice row
            ;;
            ;; the engine draws all titles highlighted, keeps one choice row
            ;; selected with inverse attribute, moves selection with up/down,
            ;; and cycles the selected value with left/right.
            ;;
            ;; 2026-06-14   tstih
            .module menu

            .include "print.inc"

            .equ    MENU_KIND_TITLE,        0
            .equ    MENU_KIND_CHOICE,       1
            .equ    MENU_ENTRY_SIZE,        11

            .equ    MENU_ACT_NONE,          0
            .equ    MENU_ACT_UP,            1
            .equ    MENU_ACT_DOWN,          2
            .equ    MENU_ACT_LEFT,          3
            .equ    MENU_ACT_RIGHT,         4
            .equ    MENU_ACT_SAVE,          5
            .equ    MENU_ACT_EXIT,          6

            .globl  kbd_read
            .globl  menu_run
            .globl  print_at

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; menu_run(<hl> *entries, <b> count, <c> initial_selected)
            ;; ----------------------------------------------------------------
            ;; runs one menu forever. the entry table stays in rom, while
            ;; choice state bytes live in sysvars or other writable memory.
            ;; ----------------------------------------------------------------
menu_run::
            ld      (menu_entries_ptr$),hl
            ld      a,b
            ld      (menu_entry_count$),a
            ld      a,c
            ld      (menu_selected$),a

            call    menu_count_choices$
            ld      (menu_choice_count$),a
            call    menu_draw$

menu_loop$:
            call    menu_read_action$
            or      a
            jr      z,menu_loop$
            cp      #MENU_ACT_UP
            jr      z,menu_up$
            cp      #MENU_ACT_DOWN
            jr      z,menu_down$
            cp      #MENU_ACT_LEFT
            jr      z,menu_left$
            cp      #MENU_ACT_RIGHT
            jr      z,menu_right$
            ;; SAVE / EXIT end the menu and return the action to the caller
            cp      #MENU_ACT_SAVE
            ret     z
            cp      #MENU_ACT_EXIT
            ret     z
            jr      menu_loop$

menu_up$:
            ld      a,(menu_selected$)
            or      a
            jr      z,menu_up_wrap$
            dec     a
            ld      (menu_selected$),a
            jr      menu_redraw$
menu_up_wrap$:
            ld      a,(menu_choice_count$)
            dec     a
            ld      (menu_selected$),a
            jr      menu_redraw$

menu_down$:
            ld      a,(menu_selected$)
            inc     a
            ld      c,a
            ld      a,(menu_choice_count$)
            cp      c
            jr      z,menu_down_wrap$
            jr      c,menu_down_wrap$
            ld      a,c
            ld      (menu_selected$),a
            jr      menu_redraw$
menu_down_wrap$:
            xor     a
            ld      (menu_selected$),a
            jr      menu_redraw$

menu_left$:
            ld      a,(menu_selected$)
            call    menu_find_choice$
            ld      bc,#8
            add     hl,bc
            ld      c,(hl)               ; c = number of choices
            inc     hl
            ld      a,(hl)               ; a = state lo
            inc     hl
            ld      h,(hl)               ; h = state hi
            ld      l,a                  ; hl = state ptr
            ld      a,(hl)
            or      a
            jr      z,menu_left_wrap$
            dec     a
            ld      (hl),a
            jr      menu_redraw$
menu_left_wrap$:
            ld      a,c
            dec     a
            ld      (hl),a
            jr      menu_redraw$

menu_right$:
            ld      a,(menu_selected$)
            call    menu_find_choice$
            ld      bc,#8
            add     hl,bc
            ld      c,(hl)               ; c = number of choices
            inc     hl
            ld      a,(hl)               ; a = state lo
            inc     hl
            ld      h,(hl)               ; h = state hi
            ld      l,a                  ; hl = state ptr
            ld      a,(hl)
            inc     a
            cp      c
            jr      c,menu_right_store$
            xor     a
menu_right_store$:
            ld      (hl),a

menu_redraw$:
            call    menu_draw$
            jp      menu_loop$

            ;; ----------------------------------------------------------------
            ;; menu_draw()
            ;; ----------------------------------------------------------------
menu_draw$:
            ld      hl,(menu_entries_ptr$)
            ld      a,(menu_entry_count$)
            ld      d,a
            xor     a
            ld      e,a                  ; e = current choice ordinal

menu_draw_loop$:
            ld      a,d
            or      a
            ret     z

            ld      a,(hl)
            cp      #MENU_KIND_TITLE
            jr      z,menu_draw_title_path$

            ld      a,(menu_selected$)
            cp      e
            ld      a,#PRINT_ATTR_NORMAL
            jr      nz,menu_draw_choice$
            ld      a,#PRINT_ATTR_INVERSE
menu_draw_choice$:
            push    de
            push    hl                  ; the draw helpers do not preserve hl,
            call    menu_draw_choice_entry$
            pop     hl                  ; so keep the entry pointer ourselves
            pop     de
            inc     e
            jr      menu_draw_next$

menu_draw_title_path$:
            push    de
            push    hl
            call    menu_draw_title_entry$
            pop     hl
            pop     de

menu_draw_next$:
            ld      bc,#MENU_ENTRY_SIZE
            add     hl,bc
            dec     d
            jr      menu_draw_loop$

menu_draw_title_entry$:
            push    hl
            inc     hl
            ld      b,(hl)
            inc     hl
            ld      c,(hl)
            inc     hl
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ld      a,#PRINT_ATTR_HIGHLIGHT
            call    print_at
            pop     hl
            ret

menu_draw_choice_entry$:
            push    af
            push    hl

            ;; label
            inc     hl
            ld      b,(hl)
            inc     hl
            ld      c,(hl)
            inc     hl
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            xor     a
            call    print_at

            pop     hl
            inc     hl
            inc     hl
            ld      c,(hl)               ; y
            inc     hl
            ld      b,(hl)               ; value x
            inc     hl
            inc     hl
            inc     hl
            ld      e,(hl)               ; choice-table lo
            inc     hl
            ld      d,(hl)               ; choice-table hi
            inc     hl
            inc     hl
            ld      a,(hl)               ; state lo
            inc     hl
            ld      h,(hl)               ; state hi
            ld      l,a                  ; hl = state ptr
            ld      a,(hl)               ; a = current choice index
            add     a,a
            ld      l,a
            ld      h,#0
            add     hl,de                ; hl = pointer-table slot
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                  ; hl = choice string
            pop     af                   ; value attribute
            push    hl
            push    bc
            push    af
            ld      hl,#menu_value_clear$
            call    print_at
            pop     af
            pop     bc
            pop     hl
            call    print_at
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= menu_count_choices()
            ;; ----------------------------------------------------------------
menu_count_choices$:
            ld      hl,(menu_entries_ptr$)
            ld      a,(menu_entry_count$)
            ld      d,a
            xor     a

menu_count_loop$:
            ld      e,a
            ld      a,d
            or      a
            ld      a,e
            ret     z
            ld      a,(hl)
            cp      #MENU_KIND_CHOICE
            jr      nz,menu_count_next$
            inc     e
menu_count_next$:
            ld      a,e
            ld      bc,#MENU_ENTRY_SIZE
            add     hl,bc
            dec     d
            jr      menu_count_loop$

            ;; ----------------------------------------------------------------
            ;; <hl> <= menu_find_choice(<a> ordinal)
            ;; ----------------------------------------------------------------
menu_find_choice$:
            ld      e,a                 ; e = remaining ordinal to skip
            ld      hl,(menu_entries_ptr$)
            ld      a,(menu_entry_count$)
            ld      d,a

menu_find_loop$:
            ld      a,(hl)
            cp      #MENU_KIND_CHOICE
            jr      nz,menu_find_next$
            ld      a,e
            or      a
            ret     z                   ; hl = the requested choice entry
            dec     e
menu_find_next$:
            ld      bc,#MENU_ENTRY_SIZE
            add     hl,bc
            dec     d
            jr      nz,menu_find_loop$
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= menu_read_action()
            ;; ----------------------------------------------------------------
menu_read_action$:
            call    kbd_read
            or      a
            jr      z,menu_read_action$
            cp      #0x1b
            jr      z,menu_read_escape$
            cp      #0x13               ; Ctrl+S = save & exit
            jr      z,menu_act_save$
            cp      #0x03               ; Ctrl+C = exit
            jr      z,menu_act_exit$
            xor     a
            ret
menu_act_save$:
            ld      a,#MENU_ACT_SAVE
            ret
menu_act_exit$:
            ld      a,#MENU_ACT_EXIT
            ret

menu_read_escape$:
            call    kbd_read
            or      a
            jr      z,menu_read_escape$
            cp      #'A'
            jr      z,menu_read_up$
            cp      #'B'
            jr      z,menu_read_down$
            cp      #'C'
            jr      z,menu_read_right$
            cp      #'D'
            jr      z,menu_read_left$
            xor     a
            ret

menu_read_up$:
            ld      a,#MENU_ACT_UP
            ret
menu_read_down$:
            ld      a,#MENU_ACT_DOWN
            ret
menu_read_left$:
            ld      a,#MENU_ACT_LEFT
            ret
menu_read_right$:
            ld      a,#MENU_ACT_RIGHT
            ret

            ;; constant blank used to wipe a value field before redraw. it must
            ;; live in _BOOT: only that area is decompressed into RAM, so data in
            ;; _SYSVARS would be uninitialized garbage at runtime.
menu_value_clear$:
            .db     ' ',' ',' ',' ',' ',' ',' ',' ',0

            .area   _SYSVARS

menu_entries_ptr$:
            .dw     0
menu_entry_count$:
            .db     0
menu_choice_count$:
            .db     0
menu_selected$:
            .db     0

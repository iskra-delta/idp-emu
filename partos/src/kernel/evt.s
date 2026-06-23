            ;; evt.s
            ;;
            ;; sync events. an event is a system object that is either in the
            ;; signaled or non-signaled state and is used by the scheduler to
            ;; block/unblock threads. hand-written replacement for yos evt.c,
            ;; smaller than the compiled C: the state is a single byte (not a
            ;; 2-byte enum) and the list walk in evt_set is inlined instead of
            ;; going through list_find / list_match_eq.
            ;;
            ;; layout (event_t):
            ;;   +0  next        (sysobj, list link)
            ;;   +2  owner       (sysobj)
            ;;   +4  state       (1 byte: 0 = non-signaled, 1 = signaled)
            ;;
            ;; public c entry points use z80 sdcccall(1); 16-bit/pointer
            ;; results are returned in de:
            ;;
            ;;   _evt_create:  in  hl = owner
            ;;                 out de = event or 0
            ;;   _evt_destroy: in  hl = event
            ;;                 out de = freed event or 0
            ;;   _evt_set:     in  hl = event, stack = newstate (1 byte)
            ;;                 out de = event or 0 (0 if not a live event)
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module evt

            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _evt_set
            .globl  __evt_first
            .globl  __so_create
            .globl  __so_destroy
            .globl  _list_find
            .globl  _list_match_eq

            .equ    EVENT_STATE,        4
            .equ    EVENT_SIZE,         5
            .equ    NONSIGNALED,        0

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _evt_create(<hl> owner)
            ;; ----------------------------------------------------------------
            ;; allocates an event sysobj, links it into the event list and
            ;; clears it to the non-signaled state.
            ;; ----------------------------------------------------------------
_evt_create::
            push    hl                  ; stack owner for __so_create
            ld      de,#EVENT_SIZE
            ld      hl,#__evt_first
            call    __so_create         ; de = event or 0; owner left on stack
            pop     hl                  ; discard owner arg
            ld      a,d
            or      e
            ret     z
            ld      hl,#EVENT_STATE
            add     hl,de
            ld      (hl),#NONSIGNALED
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _evt_destroy(<hl> event)
            ;; ----------------------------------------------------------------
_evt_destroy::
            ex      de,hl               ; de = event
            ld      hl,#__evt_first
            jp      __so_destroy

            ;; ----------------------------------------------------------------
            ;; <de> <= _evt_set(<hl> event, <stack> newstate)
            ;; ----------------------------------------------------------------
            ;; validates that the handle is still a live event by locating it
            ;; with the shared list_find / list_match_eq primitives (so list
            ;; traversal lives in one place), then writes its state and returns
            ;; the event (or 0). list_find is caller-clean for its two stacked
            ;; args; the single stacked newstate byte is cleaned on return.
            ;; ----------------------------------------------------------------
_evt_set::
            ;; build list_find(first = __evt_first, &prev, list_match_eq, event)
            push    hl                  ; prev scratch (overwritten by list_find)
            push    hl                  ; arg = event
            ld      hl,#_list_match_eq
            push    hl                  ; match
            ld      hl,#4
            add     hl,sp               ; hl = &prev scratch
            ex      de,hl               ; de = &prev
            ld      hl,(__evt_first)     ; hl = first element
            call    _list_find          ; de = event or 0
            pop     hl                  ; drop match
            pop     hl                  ; drop arg
            pop     hl                  ; drop prev scratch
            ld      a,d
            or      e
            jr      z,es_ret$
            ;; live event: state (+4) = newstate (the stacked byte at sp+2)
            ld      hl,#2
            add     hl,sp
            ld      a,(hl)              ; a = newstate
            ld      hl,#EVENT_STATE
            add     hl,de
            ld      (hl),a
es_ret$:
            pop     hl                  ; return address
            inc     sp                  ; drop 1-byte newstate arg
            jp      (hl)

            ;; the event list head now lives in page0.s (parked in the dead
            ;; pre-nmi pad); imported via the .globl at the top of this module.

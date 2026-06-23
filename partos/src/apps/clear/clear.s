            ;; clear.s
            ;;
            ;; clear the active console.
            ;;
            ;; 2026-06-23   tstih
            .module clear

            .globl  pa_init$
            .globl  pa_clear_screen$
            .globl  pa_exit_process$

            .area   _CODE

clear_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,clear_go$
clear_dead$:
            halt
            jr      clear_dead$

clear_go$:
            call    pa_clear_screen$
            call    pa_exit_process$
            jr      clear_dead$

            ;; echo.s
            ;;
            ;; write one command line back to the console.
            ;;
            ;; 2026-06-23   tstih
            .module echo

            .globl  pa_init$
            .globl  pa_arg_start$
            .globl  pa_write_buffer$
            .globl  pa_write_newline$
            .globl  pa_exit_process$

            .area   _CODE

echo_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,echo_go$
echo_dead$:
            halt
            jr      echo_dead$

echo_go$:
            call    pa_arg_start$
            push    hl
            ld      de,#0
echo_len$:
            ld      a,(hl)
            or      a
            jr      z,echo_write$
            inc     hl
            inc     de
            jr      echo_len$
echo_write$:
            pop     hl
            ld      a,d
            or      e
            jr      z,echo_nl$
            call    pa_write_buffer$
echo_nl$:
            call    pa_write_newline$
            call    pa_exit_process$
            jr      echo_dead$

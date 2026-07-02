            ;; svccall.s
            ;;
            ;; small assembly bridges for syscall-table entries that are
            ;; safer to tail-call directly than to route through a C frame.
            ;;
            ;; 2026-06-30   tstih
            .module app_svccall
            .optsdcc -mz80 sdcccall(1)

            .equ    PARTOS_OFF_EXIT_PROCESS, 70

            .globl  _app_partos
            .globl  _app_exit_process

            .area   _CODE

_app_exit_process::
            call    _app_partos         ; de = cached partos service table
            ld      a,d
            or      e
            ret     z
            ex      de,hl
            ld      bc,#PARTOS_OFF_EXIT_PROCESS
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      a,b
            or      c
            ret     z
            push    bc
            ret                         ; tail-call exit_process()

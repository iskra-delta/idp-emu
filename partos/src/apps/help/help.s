            ;; help.s
            ;;
            ;; print the built-in PartOS command set.
            ;;
            ;; 2026-06-23   tstih
            .module help

            .globl  pa_init$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .area   _CODE

help_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,help_go$
help_dead$:
            halt
            jr      help_dead$

help_go$:
            ld      hl,#help_text$
            call    pa_write_cstr$
            call    pa_exit_process$
            jr      help_dead$

help_text$:
            .db     'c','o','m','m','a','n','d','s',':',' ',0
            .db     's','h','e','l','l',',',' ','e','x','i','t',',',' ','h','e','l','p',',',' ','c','l','e','a','r',',',' ','e','c','h','o',0x0d,0x0a
            .db     ' ',' ','l','s',',',' ','p','s',',',' ','m','e','m',',',' ','c','a','t',',',' ','c','p',',',' ','m','v',',',' ','d','e','l',',',' ','r','m',0x0d,0x0a
            .db     ' ',' ','m','k','d','i','r',',',' ','r','m','d','i','r',',',' ','t','o','u','c','h',0x0d,0x0a,0

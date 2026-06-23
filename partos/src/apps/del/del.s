            ;; del.s
            ;;
            ;; delete one regular file on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module del

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_unlink_path$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATDIRENT_STATUS,          10

            .area   _CODE

del_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,del_init_evt$
del_dead$:
            halt
            jr      del_dead$

del_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jr      z,del_exit$

            call    pa_get_boot_fs$
            ld      (del_fs$),de
            ld      a,d
            or      e
            jr      z,del_error$

            call    pa_arg_start$
            ld      de,#del_path$
            ld      b,#63
            call    pa_copy_token$
            jr      c,del_usage$
            call    pa_require_eol$
            jr      nz,del_usage$

            ld      hl,(del_fs$)
            ld      de,#del_path$
            ld      bc,#del_result$
            call    pa_unlink_path$
            ld      a,d
            or      e
            jr      nz,del_error$
            call    pa_wait_one$
            ld      hl,#del_result$
            ld      bc,#FATDIRENT_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jr      nz,del_error$
            jr      del_exit$

del_usage$:
            ld      hl,#del_usage_text$
            call    pa_write_cstr$
            jr      del_exit$

del_error$:
            ld      hl,#del_error_text$
            call    pa_write_cstr$

del_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jr      del_dead$

del_usage_text$:
            .db     'u','s','a','g','e',':',' ','d','e','l',' ','P','A','T','H',0x0d,0x0a,0
del_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

del_fs$:
            .dw     0x0000
del_result$:
            .ds     12
del_path$:
            .ds     64

            ;; rm.s
            ;;
            ;; delete one regular file on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module rm

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

rm_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,rm_init_evt$
rm_dead$:
            halt
            jr      rm_dead$

rm_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jr      z,rm_exit$

            call    pa_get_boot_fs$
            ld      (rm_fs$),de
            ld      a,d
            or      e
            jr      z,rm_error$

            call    pa_arg_start$
            ld      de,#rm_path$
            ld      b,#63
            call    pa_copy_token$
            jr      c,rm_usage$
            call    pa_require_eol$
            jr      nz,rm_usage$

            ld      hl,(rm_fs$)
            ld      de,#rm_path$
            ld      bc,#rm_result$
            call    pa_unlink_path$
            ld      a,d
            or      e
            jr      nz,rm_error$
            call    pa_wait_one$
            ld      hl,#rm_result$
            ld      bc,#FATDIRENT_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jr      nz,rm_error$
            jr      rm_exit$

rm_usage$:
            ld      hl,#rm_usage_text$
            call    pa_write_cstr$
            jr      rm_exit$

rm_error$:
            ld      hl,#rm_error_text$
            call    pa_write_cstr$

rm_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jr      rm_dead$

rm_usage_text$:
            .db     'u','s','a','g','e',':',' ','r','m',' ','P','A','T','H',0x0d,0x0a,0
rm_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

rm_fs$:
            .dw     0x0000
rm_result$:
            .ds     12
rm_path$:
            .ds     64

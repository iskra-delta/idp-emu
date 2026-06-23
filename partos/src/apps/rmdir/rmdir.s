            ;; rmdir.s
            ;;
            ;; remove one empty directory on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module rmdir

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_rmdir_path$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATDIRENT_STATUS,          10

            .area   _CODE

rmdir_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,rmdir_init_evt$
rmdir_dead$:
            halt
            jr      rmdir_dead$

rmdir_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jr      z,rmdir_exit$

            call    pa_get_boot_fs$
            ld      (rmdir_fs$),de
            ld      a,d
            or      e
            jr      z,rmdir_error$

            call    pa_arg_start$
            ld      de,#rmdir_path$
            ld      b,#63
            call    pa_copy_token$
            jr      c,rmdir_usage$
            call    pa_require_eol$
            jr      nz,rmdir_usage$

            ld      hl,(rmdir_fs$)
            ld      de,#rmdir_path$
            ld      bc,#rmdir_result$
            call    pa_rmdir_path$
            ld      a,d
            or      e
            jr      nz,rmdir_error$
            call    pa_wait_one$
            ld      hl,#rmdir_result$
            ld      bc,#FATDIRENT_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jr      nz,rmdir_error$
            jr      rmdir_exit$

rmdir_usage$:
            ld      hl,#rmdir_usage_text$
            call    pa_write_cstr$
            jr      rmdir_exit$

rmdir_error$:
            ld      hl,#rmdir_error_text$
            call    pa_write_cstr$

rmdir_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jr      rmdir_dead$

rmdir_usage_text$:
            .db     'u','s','a','g','e',':',' ','r','m','d','i','r',' ','P','A','T','H',0x0d,0x0a,0
rmdir_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

rmdir_fs$:
            .dw     0x0000
rmdir_result$:
            .ds     12
rmdir_path$:
            .ds     64

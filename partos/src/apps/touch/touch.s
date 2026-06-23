            ;; touch.s
            ;;
            ;; create one empty file on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module touch

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_create_file$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATFILE_STATUS,            10

            .area   _CODE

touch_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,touch_init_evt$
touch_dead$:
            halt
            jr      touch_dead$

touch_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jr      z,touch_exit$

            call    pa_get_boot_fs$
            ld      (touch_fs$),de
            ld      a,d
            or      e
            jr      z,touch_error$

            call    pa_arg_start$
            ld      de,#touch_path$
            ld      b,#63
            call    pa_copy_token$
            jr      c,touch_usage$
            call    pa_require_eol$
            jr      nz,touch_usage$

            ld      hl,(touch_fs$)
            ld      de,#touch_path$
            ld      bc,#touch_file$
            call    pa_create_file$
            ld      a,d
            or      e
            jr      nz,touch_error$
            call    pa_wait_one$
            ld      hl,#touch_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jr      nz,touch_error$
            jr      touch_exit$

touch_usage$:
            ld      hl,#touch_usage_text$
            call    pa_write_cstr$
            jr      touch_exit$

touch_error$:
            ld      hl,#touch_error_text$
            call    pa_write_cstr$

touch_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jr      touch_dead$

touch_usage_text$:
            .db     'u','s','a','g','e',':',' ','t','o','u','c','h',' ','P','A','T','H',0x0d,0x0a,0
touch_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

touch_fs$:
            .dw     0x0000
touch_file$:
            .ds     18
touch_path$:
            .ds     64

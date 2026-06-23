            ;; mkdir.s
            ;;
            ;; create one directory on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module mkdir

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_mkdir_path$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATDIRENT_STATUS,          10

            .area   _CODE

mkdir_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,mkdir_init_evt$
mkdir_dead$:
            halt
            jr      mkdir_dead$

mkdir_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jr      z,mkdir_exit$

            call    pa_get_boot_fs$
            ld      (mkdir_fs$),de
            ld      a,d
            or      e
            jr      z,mkdir_error$

            call    pa_arg_start$
            ld      de,#mkdir_path$
            ld      b,#63
            call    pa_copy_token$
            jr      c,mkdir_usage$
            call    pa_require_eol$
            jr      nz,mkdir_usage$

            ld      hl,(mkdir_fs$)
            ld      de,#mkdir_path$
            ld      bc,#mkdir_result$
            call    pa_mkdir_path$
            ld      a,d
            or      e
            jr      nz,mkdir_error$
            call    pa_wait_one$
            ld      hl,#mkdir_result$
            ld      bc,#FATDIRENT_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jr      nz,mkdir_error$
            jr      mkdir_exit$

mkdir_usage$:
            ld      hl,#mkdir_usage_text$
            call    pa_write_cstr$
            jr      mkdir_exit$

mkdir_error$:
            ld      hl,#mkdir_error_text$
            call    pa_write_cstr$

mkdir_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jr      mkdir_dead$

mkdir_usage_text$:
            .db     'u','s','a','g','e',':',' ','m','k','d','i','r',' ','P','A','T','H',0x0d,0x0a,0
mkdir_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

mkdir_fs$:
            .dw     0x0000
mkdir_result$:
            .ds     12
mkdir_path$:
            .ds     64

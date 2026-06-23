            ;; cat.s
            ;;
            ;; dump one sector-aligned file from the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module cat

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_open_file$
            .globl  pa_read_file$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_buffer$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATFILE_SIZE,              2
            .equ    FATFILE_STATUS,            10

            .area   _CODE

cat_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,cat_init_evt$
cat_dead$:
            halt
            jr      cat_dead$

cat_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jp      z,cat_exit$

            call    pa_get_boot_fs$
            ld      (cat_fs$),de
            ld      a,d
            or      e
            jp      z,cat_error$

            call    pa_arg_start$
            ld      de,#cat_path$
            ld      b,#63
            call    pa_copy_token$
            jp      c,cat_usage$
            call    pa_require_eol$
            jp      nz,cat_usage$

            ld      hl,(cat_fs$)
            ld      de,#cat_path$
            ld      bc,#cat_file$
            call    pa_open_file$
            ld      a,d
            or      e
            jp      nz,cat_error$
            call    pa_wait_one$
            ld      hl,#cat_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cat_error$

            ld      a,(cat_file$ + FATFILE_SIZE)
            or      a
            jp      nz,cat_align_error$
            ld      a,(cat_file$ + FATFILE_SIZE + 3)
            or      a
            jp      nz,cat_align_error$
            ld      a,(cat_file$ + FATFILE_SIZE + 1)
            ld      (cat_secs$),a
            ld      a,(cat_file$ + FATFILE_SIZE + 2)
            ld      (cat_secs$ + 1),a

cat_loop$:
            ld      hl,(cat_secs$)
            ld      a,h
            or      l
            jp      z,cat_exit$
            ld      hl,#cat_file$
            ld      de,#cat_buf$
            ld      bc,#256
            call    pa_read_file$
            ld      a,d
            or      e
            jp      nz,cat_error$
            call    pa_wait_one$
            ld      hl,#cat_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cat_error$
            ld      hl,#cat_buf$
            ld      de,#256
            call    pa_write_buffer$
            ld      hl,(cat_secs$)
            dec     hl
            ld      (cat_secs$),hl
            jr      cat_loop$

cat_usage$:
            ld      hl,#cat_usage_text$
            call    pa_write_cstr$
            jp      cat_exit$

cat_align_error$:
            ld      hl,#cat_align_text$
            call    pa_write_cstr$
            jp      cat_exit$

cat_error$:
            ld      hl,#cat_error_text$
            call    pa_write_cstr$

cat_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jp      cat_dead$

cat_usage_text$:
            .db     'u','s','a','g','e',':',' ','c','a','t',' ','P','A','T','H',0x0d,0x0a,0
cat_align_text$:
            .db     'o','n','l','y',' ','2','5','6','-','b','y','t','e',' ','a','l','i','g','n','e','d',' ','f','i','l','e','s',0x0d,0x0a,0
cat_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

cat_fs$:
            .dw     0x0000
cat_secs$:
            .dw     0x0000
cat_file$:
            .ds     18
cat_path$:
            .ds     64
cat_buf$:
            .ds     256

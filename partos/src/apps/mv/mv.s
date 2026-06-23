            ;; mv.s
            ;;
            ;; move one sector-aligned file by copy-then-delete.
            ;;
            ;; 2026-06-23   tstih
            .module mv

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_open_file$
            .globl  pa_create_file$
            .globl  pa_read_file$
            .globl  pa_write_file$
            .globl  pa_unlink_path$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATFILE_SIZE,              2
            .equ    FATFILE_STATUS,            10
            .equ    FATDIRENT_STATUS,          10

            .area   _CODE

mv_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,mv_init_evt$
mv_dead$:
            halt
            jr      mv_dead$

mv_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jp      z,mv_exit$

            call    pa_get_boot_fs$
            ld      (mv_fs$),de
            ld      a,d
            or      e
            jp      z,mv_error$

            call    pa_arg_start$
            ld      de,#mv_src_path$
            ld      b,#63
            call    pa_copy_token$
            jp      c,mv_usage$
            ld      de,#mv_dst_path$
            ld      b,#63
            call    pa_copy_token$
            jp      c,mv_usage$
            call    pa_require_eol$
            jp      nz,mv_usage$

            ld      hl,(mv_fs$)
            ld      de,#mv_src_path$
            ld      bc,#mv_src_file$
            call    pa_open_file$
            ld      a,d
            or      e
            jp      nz,mv_error$
            call    pa_wait_one$
            ld      hl,#mv_src_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,mv_error$

            ld      a,(mv_src_file$ + FATFILE_SIZE)
            or      a
            jp      nz,mv_align_error$
            ld      a,(mv_src_file$ + FATFILE_SIZE + 3)
            or      a
            jp      nz,mv_align_error$
            ld      a,(mv_src_file$ + FATFILE_SIZE + 1)
            ld      (mv_secs$),a
            ld      a,(mv_src_file$ + FATFILE_SIZE + 2)
            ld      (mv_secs$ + 1),a

            ld      hl,(mv_fs$)
            ld      de,#mv_dst_path$
            ld      bc,#mv_dst_file$
            call    pa_create_file$
            ld      a,d
            or      e
            jp      nz,mv_error$
            call    pa_wait_one$
            ld      hl,#mv_dst_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,mv_error$

mv_copy_loop$:
            ld      hl,(mv_secs$)
            ld      a,h
            or      l
            jp      z,mv_delete_src$
            ld      hl,#mv_src_file$
            ld      de,#mv_buf$
            ld      bc,#256
            call    pa_read_file$
            ld      a,d
            or      e
            jp      nz,mv_error$
            call    pa_wait_one$
            ld      hl,#mv_src_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,mv_error$

            ld      hl,#mv_dst_file$
            ld      de,#mv_buf$
            ld      bc,#256
            call    pa_write_file$
            ld      a,d
            or      e
            jp      nz,mv_error$
            call    pa_wait_one$
            ld      hl,#mv_dst_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,mv_error$

            ld      hl,(mv_secs$)
            dec     hl
            ld      (mv_secs$),hl
            jr      mv_copy_loop$

mv_delete_src$:
            ld      hl,(mv_fs$)
            ld      de,#mv_src_path$
            ld      bc,#mv_result$
            call    pa_unlink_path$
            ld      a,d
            or      e
            jp      nz,mv_error$
            call    pa_wait_one$
            ld      hl,#mv_result$
            ld      bc,#FATDIRENT_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,mv_error$
            jp      mv_exit$

mv_usage$:
            ld      hl,#mv_usage_text$
            call    pa_write_cstr$
            jp      mv_exit$

mv_align_error$:
            ld      hl,#mv_align_text$
            call    pa_write_cstr$
            jp      mv_exit$

mv_error$:
            ld      hl,#mv_error_text$
            call    pa_write_cstr$

mv_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jp      mv_dead$

mv_usage_text$:
            .db     'u','s','a','g','e',':',' ','m','v',' ','S','R','C',' ','D','S','T',0x0d,0x0a,0
mv_align_text$:
            .db     'o','n','l','y',' ','2','5','6','-','b','y','t','e',' ','a','l','i','g','n','e','d',' ','f','i','l','e','s',0x0d,0x0a,0
mv_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

mv_fs$:
            .dw     0x0000
mv_secs$:
            .dw     0x0000
mv_src_file$:
            .ds     18
mv_dst_file$:
            .ds     18
mv_result$:
            .ds     12
mv_src_path$:
            .ds     64
mv_dst_path$:
            .ds     64
mv_buf$:
            .ds     256

            ;; cp.s
            ;;
            ;; copy one sector-aligned file on the boot filesystem.
            ;;
            ;; 2026-06-23   tstih
            .module cp

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
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FATFILE_SIZE,              2
            .equ    FATFILE_STATUS,            10

            .area   _CODE

cp_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,cp_init_evt$
cp_dead$:
            halt
            jr      cp_dead$

cp_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jp      z,cp_exit$

            call    pa_get_boot_fs$
            ld      (cp_fs$),de
            ld      a,d
            or      e
            jp      z,cp_error$

            call    pa_arg_start$
            ld      de,#cp_src_path$
            ld      b,#63
            call    pa_copy_token$
            jp      c,cp_usage$
            ld      de,#cp_dst_path$
            ld      b,#63
            call    pa_copy_token$
            jp      c,cp_usage$
            call    pa_require_eol$
            jp      nz,cp_usage$

            ld      hl,(cp_fs$)
            ld      de,#cp_src_path$
            ld      bc,#cp_src_file$
            call    pa_open_file$
            ld      a,d
            or      e
            jp      nz,cp_error$
            call    pa_wait_one$
            ld      hl,#cp_src_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cp_error$

            ld      a,(cp_src_file$ + FATFILE_SIZE)
            or      a
            jp      nz,cp_align_error$
            ld      a,(cp_src_file$ + FATFILE_SIZE + 3)
            or      a
            jp      nz,cp_align_error$
            ld      a,(cp_src_file$ + FATFILE_SIZE + 1)
            ld      (cp_secs$),a
            ld      a,(cp_src_file$ + FATFILE_SIZE + 2)
            ld      (cp_secs$ + 1),a

            ld      hl,(cp_fs$)
            ld      de,#cp_dst_path$
            ld      bc,#cp_dst_file$
            call    pa_create_file$
            ld      a,d
            or      e
            jp      nz,cp_error$
            call    pa_wait_one$
            ld      hl,#cp_dst_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cp_error$

cp_loop$:
            ld      hl,(cp_secs$)
            ld      a,h
            or      l
            jp      z,cp_exit$
            ld      hl,#cp_src_file$
            ld      de,#cp_buf$
            ld      bc,#256
            call    pa_read_file$
            ld      a,d
            or      e
            jp      nz,cp_error$
            call    pa_wait_one$
            ld      hl,#cp_src_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cp_error$

            ld      hl,#cp_dst_file$
            ld      de,#cp_buf$
            ld      bc,#256
            call    pa_write_file$
            ld      a,d
            or      e
            jp      nz,cp_error$
            call    pa_wait_one$
            ld      hl,#cp_dst_file$
            ld      bc,#FATFILE_STATUS
            call    pa_status_at$
            ld      a,h
            or      l
            jp      nz,cp_error$

            ld      hl,(cp_secs$)
            dec     hl
            ld      (cp_secs$),hl
            jr      cp_loop$

cp_usage$:
            ld      hl,#cp_usage_text$
            call    pa_write_cstr$
            jp      cp_exit$

cp_align_error$:
            ld      hl,#cp_align_text$
            call    pa_write_cstr$
            jp      cp_exit$

cp_error$:
            ld      hl,#cp_error_text$
            call    pa_write_cstr$

cp_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jp      cp_dead$

cp_usage_text$:
            .db     'u','s','a','g','e',':',' ','c','p',' ','S','R','C',' ','D','S','T',0x0d,0x0a,0
cp_align_text$:
            .db     'o','n','l','y',' ','2','5','6','-','b','y','t','e',' ','a','l','i','g','n','e','d',' ','f','i','l','e','s',0x0d,0x0a,0
cp_error_text$:
            .db     '?',0x0d,0x0a,0

            .area   _INITIALIZED

cp_fs$:
            .dw     0x0000
cp_secs$:
            .dw     0x0000
cp_src_file$:
            .ds     18
cp_dst_file$:
            .ds     18
cp_src_path$:
            .ds     64
cp_dst_path$:
            .ds     64
cp_buf$:
            .ds     256

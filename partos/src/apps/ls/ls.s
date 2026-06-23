            ;; ls.s
            ;;
            ;; list one directory on the boot filesystem.
            ;;
            ;; usage:
            ;;   ls           -> list "/"
            ;;   ls /PATH     -> list one absolute directory
            ;;
            ;; 2026-06-23   tstih
            .module ls

            .globl  pa_init$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_get_boot_fs$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$
            .globl  pa_readdir$
            .globl  pa_wait_one$
            .globl  pa_status_at$
            .globl  pa_write_buffer$
            .globl  pa_write_cstr$
            .globl  pa_exit_process$

            .equ    FAT_ENOENT,                 0xfffa
            .equ    FAT_ATTR_VOLUME_ID,         0x08
            .equ    FAT_ATTR_DIRECTORY,         0x10
            .equ    FATDIRINFO_ATTR,            9
            .equ    FATDIRINFO_STATUS,          10
            .equ    FATDIRINFO_INDEX,           12
            .equ    FATDIRINFO_NAME,            14

            .area   _CODE

ls_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,ls_init_evt$
ls_dead$:
            halt
            jr      ls_dead$

ls_init_evt$:
            call    pa_create_event$
            ld      a,d
            or      e
            jp      z,ls_exit$

            call    pa_get_boot_fs$
            ld      (ls_fs$),de
            ld      a,d
            or      e
            jp      z,ls_error$

            call    ls_prepare_path$
            jp      c,ls_usage$

            xor     a
            ld      (ls_dirinfo$ + FATDIRINFO_INDEX),a
            ld      (ls_dirinfo$ + FATDIRINFO_INDEX + 1),a

ls_loop$:
            ld      hl,(ls_fs$)
            ld      de,#ls_path$
            ld      bc,#ls_dirinfo$
            call    pa_readdir$
            ld      a,d
            or      e
            jp      nz,ls_error$
            call    pa_wait_one$
            ld      hl,#ls_dirinfo$
            ld      bc,#FATDIRINFO_STATUS
            call    pa_status_at$
            ld      a,h
            cp      #0xff
            jr      nz,ls_status_other$
            ld      a,l
            cp      #0xfa
            jp      z,ls_exit$
ls_status_other$:
            ld      a,h
            or      l
            jp      nz,ls_error$

            ld      hl,#(ls_dirinfo$ + FATDIRINFO_INDEX)
            inc     (hl)
            jr      nz,ls_index_done$
            inc     hl
            inc     (hl)
ls_index_done$:

            ld      a,(ls_dirinfo$ + FATDIRINFO_ATTR)
            and     #FAT_ATTR_VOLUME_ID
            jr      nz,ls_loop$

            call    ls_format_line$
            jp      c,ls_error$
            call    pa_write_buffer$
            jr      ls_loop$

ls_prepare_path$:
            call    pa_arg_start$
            ld      a,(hl)
            or      a
            jr      nz,ls_have_arg$
            ld      hl,#ls_root_path$
            ld      de,#ls_path$
ls_copy_default$:
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            or      a
            ret     z
            jr      ls_copy_default$

ls_have_arg$:
            ld      de,#ls_path$
            ld      b,#63
            call    pa_copy_token$
            ret     c
            call    pa_require_eol$
            ret     z
            scf
            ret

ls_format_line$:
            ld      hl,#(ls_dirinfo$ + FATDIRINFO_NAME)
            ld      de,#ls_line$
            ld      b,#8
            xor     a
            ld      (ls_has_base$),a
ls_base_loop$:
            ld      a,(hl)
            cp      #' '
            jr      z,ls_base_done$
            ld      (de),a
            inc     de
            inc     hl
            ld      a,#1
            ld      (ls_has_base$),a
            djnz    ls_base_loop$
ls_base_done$:
            ld      a,(ls_has_base$)
            or      a
            jr      nz,ls_ext_scan$
            scf
            ret

ls_ext_scan$:
            ld      hl,#(ls_dirinfo$ + FATDIRINFO_NAME + 8)
            ld      b,#3
            xor     a
            ld      (ls_has_ext$),a
ls_ext_probe$:
            ld      a,(hl)
            cp      #' '
            jr      nz,ls_ext_yes$
            inc     hl
            djnz    ls_ext_probe$
            jr      ls_ext_done$
ls_ext_yes$:
            ld      a,#1
            ld      (ls_has_ext$),a

ls_ext_done$:
            ld      a,(ls_has_ext$)
            or      a
            jr      z,ls_dir_mark$
            ld      a,#'.'
            ld      (de),a
            inc     de
            ld      hl,#(ls_dirinfo$ + FATDIRINFO_NAME + 8)
            ld      b,#3
ls_ext_copy$:
            ld      a,(hl)
            cp      #' '
            jr      z,ls_dir_mark$
            ld      (de),a
            inc     de
            inc     hl
            djnz    ls_ext_copy$

ls_dir_mark$:
            ld      a,(ls_dirinfo$ + FATDIRINFO_ATTR)
            and     #FAT_ATTR_DIRECTORY
            jr      z,ls_finish_line$
            ld      a,#'/'
            ld      (de),a
            inc     de

ls_finish_line$:
            ld      a,#0x0d
            ld      (de),a
            inc     de
            ld      a,#0x0a
            ld      (de),a
            inc     de
            xor     a
            ld      (de),a
            push    de
            pop     hl
            ld      bc,#ls_line$
            or      a
            sbc     hl,bc
            ex      de,hl
            ld      hl,#ls_line$
            or      a
            ret

ls_usage$:
            ld      hl,#ls_usage_text$
            call    pa_write_cstr$
            jr      ls_exit$

ls_error$:
            ld      hl,#ls_error_text$
            call    pa_write_cstr$

ls_exit$:
            call    pa_destroy_event$
            call    pa_exit_process$
            jp      ls_dead$

ls_usage_text$:
            .db     'u','s','a','g','e',':',' ','l','s',' ','[','/','P','A','T','H',']',0x0d,0x0a,0
ls_error_text$:
            .db     '?',0x0d,0x0a,0
ls_root_path$:
            .db     '/',0

            .area   _INITIALIZED

ls_fs$:
            .dw     0x0000
ls_has_base$:
            .db     0x00
ls_has_ext$:
            .db     0x00
ls_dirinfo$:
            .ds     25
ls_path$:
            .ds     64
ls_line$:
            .ds     18

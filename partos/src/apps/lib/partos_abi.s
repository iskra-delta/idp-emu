            ;; partos_abi.s
            ;;
            ;; thin sdcccall(1) wrappers around the hand-written userland
            ;; helper layer in partos.s. this keeps the app logic in C while
            ;; reusing the existing service-table glue unchanged.
            ;;
            ;; 2026-06-27   tstih
            .module partos_abi

            .globl  _pa_init
            .globl  _pa_dead
            .globl  _pa_clear_screen
            .globl  _pa_write_buffer
            .globl  _pa_query_service
            .globl  _pa_bind_event
            .globl  _pa_create_event
            .globl  _pa_destroy_event
            .globl  _pa_wait_one
            .globl  _pa_get_sys_info
            .globl  _pa_get_boot_fs
            .globl  _pa_get_current_dir
            .globl  _pa_mount_fs
            .globl  _pa_get_command_line
            .globl  _pa_exit_process
            .globl  _pa_lookup_path
            .globl  _pa_open_file
            .globl  _pa_create_file
            .globl  _pa_read_file
            .globl  _pa_write_file
            .globl  _pa_readdir
            .globl  _pa_unlink_path
            .globl  _pa_mkdir_path
            .globl  _pa_rmdir_path
            .globl  _pa_set_text_attr
            .globl  _app_format_dir_line

            .globl  pa_init$
            .globl  pa_clear_screen$
            .globl  pa_write_buffer$
            .globl  pa_query_service$
            .globl  pa_bind_event$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_wait_one$
            .globl  pa_get_sys_info$
            .globl  pa_get_boot_fs$
            .globl  pa_get_current_dir$
            .globl  pa_mount_fs$
            .globl  pa_get_command_line$
            .globl  pa_exit_process$
            .globl  pa_lookup_path$
            .globl  pa_open_file$
            .globl  pa_create_file$
            .globl  pa_read_file$
            .globl  pa_write_file$
            .globl  pa_readdir$
            .globl  pa_unlink_path$
            .globl  pa_mkdir_path$
            .globl  pa_rmdir_path$
            .globl  pa_set_text_attr$

            .equ    FAT_ATTR_DIRECTORY,     0x10
            .equ    FATDIRINFO_ATTR,        9
            .equ    FATDIRINFO_NAME,        14

            .area   _CODE

_pa_init::
            jp      pa_init$

_pa_dead::
pa_dead_loop$:
            halt
            jr      pa_dead_loop$

_pa_clear_screen::
            jp      pa_clear_screen$

_pa_write_buffer::
            jp      pa_write_buffer$

_pa_query_service::
            jp      pa_query_service$

_pa_bind_event::
            jp      pa_bind_event$

_pa_create_event::
            jp      pa_create_event$

_pa_destroy_event::
            jp      pa_destroy_event$

_pa_wait_one::
            jp      pa_wait_one$

_pa_get_sys_info::
            jp      pa_get_sys_info$

_pa_get_boot_fs::
            jp      pa_get_boot_fs$

_pa_get_current_dir::
            jp      pa_get_current_dir$

_pa_mount_fs::
            jp      pa_mount_fs$

_pa_get_command_line::
            jp      pa_get_command_line$

_pa_exit_process::
            jp      pa_exit_process$

_pa_lookup_path::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_lookup_path$
            jr      pa_ret_drop2$

_pa_open_file::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_open_file$
            jr      pa_ret_drop2$

_pa_create_file::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_create_file$
            jr      pa_ret_drop2$

_pa_read_file::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_read_file$
            jr      pa_ret_drop2$

_pa_write_file::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_write_file$
            jr      pa_ret_drop2$

_pa_readdir::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_readdir$
            jr      pa_ret_drop2$

_pa_unlink_path::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_unlink_path$
            jr      pa_ret_drop2$

_pa_mkdir_path::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_mkdir_path$
            jr      pa_ret_drop2$

_pa_rmdir_path::
            push    hl
            push    de
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            pop     de
            pop     hl
            call    pa_rmdir_path$

pa_ret_drop2$:
            pop     hl
            inc     sp
            inc     sp
            jp      (hl)

_pa_set_text_attr::
            jp      pa_set_text_attr$

_app_format_dir_line::
            ld      bc,#FATDIRINFO_NAME
            add     hl,bc
            push    hl                  ; keep name start for ext/attr probes
            ld      b,#8
            xor     a
            ld      c,a
afdl_base_loop$:
            ld      a,(hl)
            cp      #' '
            jr      z,afdl_base_done$
            ld      (de),a
            inc     de
            inc     hl
            ld      c,#1
            djnz    afdl_base_loop$
afdl_base_done$:
            ld      a,c
            or      a
            jr      nz,afdl_ext_scan$
            pop     hl
            xor     a
            ret

afdl_ext_scan$:
            pop     hl
            push    hl
            ld      bc,#8
            add     hl,bc
            ld      b,#3
            xor     a
            ld      c,a
afdl_ext_probe$:
            ld      a,(hl)
            cp      #' '
            jr      nz,afdl_ext_yes$
            inc     hl
            djnz    afdl_ext_probe$
            jr      afdl_ext_done$
afdl_ext_yes$:
            ld      c,#1
afdl_ext_done$:
            ld      a,c
            or      a
            jr      z,afdl_dir_mark$
            ld      a,#'.'
            ld      (de),a
            inc     de
            pop     hl
            push    hl
            ld      bc,#8
            add     hl,bc
            ld      b,#3
afdl_ext_copy$:
            ld      a,(hl)
            cp      #' '
            jr      z,afdl_dir_mark$
            ld      (de),a
            inc     de
            inc     hl
            djnz    afdl_ext_copy$

afdl_dir_mark$:
            pop     hl
            push    hl
            dec     hl
            dec     hl
            dec     hl
            dec     hl
            dec     hl                  ; hl = info->attr
            ld      a,(hl)
            and     #FAT_ATTR_DIRECTORY
            jr      z,afdl_finish$
            ld      a,#'/'
            ld      (de),a
            inc     de

afdl_finish$:
            ld      a,#0x0d
            ld      (de),a
            inc     de
            ld      a,#0x0a
            ld      (de),a
            inc     de
            xor     a
            ld      (de),a
            pop     hl
            ld      a,#1
            ret

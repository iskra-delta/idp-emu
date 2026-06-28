            ;; shell.s
            ;;
            ;; first PartOS shell payload.
            ;;
            ;; this is a flat payload linked at address 0. tools/makecom.py
            ;; scans every symbol whose name starts with "__reloc_" and turns
            ;; those word locations into XL relocation entries before it wraps
            ;; the image in a COM header.
            ;;
            ;; the shell itself talks through the public "partos" service and
            ;; also registers one lightweight shell-owned "libc" service for
            ;; process launch metadata and prompt helpers.
            ;;
            ;; 2026-06-22   tstih
            .module shell

            .equ    PARTOS_OFF_GET_SYS_INFO,    0
            .equ    PARTOS_OFF_CLEAR_SCREEN,    2
            .equ    PARTOS_OFF_WRITE_CONSOLE,   6
            .equ    PARTOS_OFF_READ_KEYBOARD,   10
            .equ    PARTOS_OFF_RUN_COMMAND,     12
            .equ    PARTOS_OFF_ALLOCATE_MEMORY, 26
            .equ    PARTOS_OFF_REGISTER_SERVICE,38
            .equ    PARTOS_OFF_EXIT_PROCESS,    70
            .equ    PARTOS_OFF_GET_BOOT_FS,     88
            .equ    PARTOS_OFF_WAIT_PROCESS,    100
            .equ    SVC_NAME,                  4
            .equ    SVC_FNTABLE,               20

            .equ    THREAD_PROCESS,             22
            .equ    PROCESS_CMDLINE,            15
            .equ    PROCESS_ENV,                17
            .equ    SYSINFO_CURRENT_THREAD,     24
            .equ    SYSINFO_USER_HEAP,          36

            ;; export relocation markers so the linker map retains them for
            ;; tools/makecom.py; otherwise the XL wrapper sees reloc_count = 0
            ;; and every absolute shell pointer stays unrelocated.
            .globl  __reloc_entry_init$
            .globl  __reloc_entry_clear$
            .globl  __reloc_entry_banner$
            .globl  __reloc_prompt_len$
            .globl  __reloc_prompt_write$
            .globl  __reloc_read_key$
            .globl  __reloc_read_len$
            .globl  __reloc_read_buf$
            .globl  __reloc_read_len_inc$
            .globl  __reloc_store_len_inc$
            .globl  __reloc_echo_char$
            .globl  __reloc_dispatch_nl$
            .globl  __reloc_dispatch_len$
            .globl  __reloc_dispatch_buf$
            .globl  __reloc_dispatch_run$
            .globl  __reloc_dispatch_wait$
            .globl  __reloc_dispatch_err$
            .globl  __reloc_init_name$
            .globl  __reloc_init_store_partos$
            .globl  __reloc_init_store_partos_reload$
            .globl  __reloc_init_shell_name$
            .globl  __reloc_init_shell_table$
            .globl  __reloc_init_shell_table_repair$
            .globl  __reloc_init_store_shellsvc$
            .globl  __reloc_regsvc_off$
            .globl  __reloc_regsvc_store_tmp$
            .globl  __reloc_regsvc_call$
            .globl  __reloc_calloff_partos$
            .globl  __reloc_clear_off$
            .globl  __reloc_clear_call$
            .globl  __reloc_rk_off$
            .globl  __reloc_rk_call$
            .globl  __reloc_run_buf$
            .globl  __reloc_run_store_tmp$
            .globl  __reloc_run_off$
            .globl  __reloc_run_load_tmp$
            .globl  __reloc_run_attach$
            .globl  __reloc_wait_calloff$
            .globl  __reloc_wait_store_tmp$
            .globl  __reloc_wait_load_tmp$
            .globl  __reloc_wb_off$
            .globl  __reloc_echo_store$
            .globl  __reloc_echo_ptr$
            .globl  __reloc_echo_write$
            .globl  __reloc_banner_ptr$
            .globl  __reloc_banner_write$
            .globl  __reloc_gd_off$
            .globl  __reloc_gd_call$
            .globl  __reloc_prompt_dev$
            .globl  __reloc_alias_dev$
            .globl  __reloc_alias_hd0$
            .globl  __reloc_alias_hd1$
            .globl  __reloc_prompt_dev_write$
            .globl  __reloc_prompt_ptr2$
            .globl  __reloc_prompt_write2$
            .globl  __reloc_nl_ptr$
            .globl  __reloc_nl_write$
            .globl  __reloc_err_ptr$
            .globl  __reloc_err_write2$
            .globl  __reloc_wcs_write$
            .globl  __reloc_norm_buf_in$
            .globl  __reloc_norm_buf_out$
            .globl  __reloc_norm_len_store$
            .globl  __reloc_norm_len_load$
            .globl  __reloc_norm_trim_buf$
            .globl  __reloc_norm_trim_store$
            .globl  __reloc_exit_len$
            .globl  __reloc_exit_buf$
            .globl  __reloc_dispatch_len2$
            .globl  __reloc_dispatch_normalize$
            .globl  __reloc_dispatch_is_exit$
            .globl  __reloc_dispatch_exit$
            .globl  __reloc_norm_skip$
            .globl  __reloc_erase_len$
            .globl  __reloc_erase_store$
            .globl  __reloc_erase_buf$
            .globl  __reloc_erase_call$
            .globl  __reloc_erase_ptr$
            .globl  __reloc_erase_write$
            .globl  __reloc_exit_upper0$
            .globl  __reloc_exit_upper1$
            .globl  __reloc_exit_upper2$
            .globl  __reloc_exit_upper3$
            .globl  __reloc_exit_calloff$
            .globl  __reloc_gsi_off$
            .globl  __reloc_gsi_call$
            .globl  __reloc_guh_sysinfo$
            .globl  __reloc_gcp_sysinfo$
            .globl  __reloc_gcl_process$
            .globl  __reloc_gcl_empty$
            .globl  __reloc_genv_process$
            .globl  __reloc_genv_empty$
            .globl  __reloc_getenv_name_store$
            .globl  __reloc_getenv_env_call$
            .globl  __reloc_getenv_name_load$
            .globl  __reloc_attach_empty_env$
            .globl  __reloc_attach_alloc_off$
            .globl  __reloc_attach_alloc_store$
            .globl  __reloc_attach_heap_call$
            .globl  __reloc_attach_len$
            .globl  __reloc_attach_alloc_load$
            .globl  __reloc_attach_src$
            .globl  __reloc_attach_len2$
            .globl  __reloc_attach_empty_cmd$
            .globl  __reloc_run_result_store$
            .globl  __reloc_libcsvc_getcmd$
            .globl  __reloc_libcsvc_getenv$
            .globl  __reloc_libcsvc_getenvvar$
            .globl  __reloc_libcsvc_getdev$
            .globl  __reloc_libcsvc_prompt$
            .globl  shell_run_command$
            .globl  shell_wait_process$
            .globl  shell_attach_launch$
            .globl  shell_registered_service$
            .globl  shell_tmp_ptr$
            .globl  shell_run_result_debug$
            .globl  shell_wait_input_debug$

            .area   _CODE

shell_entry::
            .db     0xcd
__reloc_entry_init$:
            .dw     shell_init$
            ld      a,d
            or      e
            jr      nz,shell_ready$

shell_halt$:
            halt
            jr      shell_halt$

shell_ready$:
            .db     0xcd
__reloc_entry_clear$:
            .dw     shell_clear$
            .db     0xcd
__reloc_entry_banner$:
            .dw     shell_write_banner$

shell_prompt$:
            xor     a
            .db     0x32
__reloc_prompt_len$:
            .dw     shell_cmd_len$
            .db     0xcd
__reloc_prompt_write$:
            .dw     shell_write_prompt$

shell_read_loop$:
            .db     0xcd
__reloc_read_key$:
            .dw     shell_read_key$
            ld      a,e
            cp      #0x0d
            jr      z,shell_dispatch$
            cp      #0x0a
            jr      z,shell_dispatch$
            cp      #0x08
            jr      z,shell_erase$
            cp      #0x7f
            jr      z,shell_erase$
            cp      #0x20
            jr      c,shell_read_loop$

            .db     0x3a
__reloc_read_len$:
            .dw     shell_cmd_len$
            cp      #63
            jr      nc,shell_read_loop$

            ld      c,a
            ld      b,#0
            .db     0x21
__reloc_read_buf$:
            .dw     shell_cmd_buf$
            add     hl,bc
            ld      (hl),e

            .db     0x3a
__reloc_read_len_inc$:
            .dw     shell_cmd_len$
            inc     a
            .db     0x32
__reloc_store_len_inc$:
            .dw     shell_cmd_len$

            ld      a,e
            .db     0xcd
__reloc_echo_char$:
            .dw     shell_echo_char$
            jr      shell_read_loop$

shell_erase$:
            .db     0x3a
__reloc_erase_len$:
            .dw     shell_cmd_len$
            or      a
            jr      z,shell_read_loop$
            dec     a
            .db     0x32
__reloc_erase_store$:
            .dw     shell_cmd_len$
            ld      c,a
            ld      b,#0
            .db     0x21
__reloc_erase_buf$:
            .dw     shell_cmd_buf$
            add     hl,bc
            xor     a
            ld      (hl),a
            .db     0xcd
__reloc_erase_call$:
            .dw     shell_write_erase$
            jr      shell_read_loop$

shell_dispatch$:
            .db     0xcd
__reloc_dispatch_nl$:
            .dw     shell_write_newline$

            .db     0x3a
__reloc_dispatch_len$:
            .dw     shell_cmd_len$
            ld      c,a
            ld      b,#0
            .db     0x21
__reloc_dispatch_buf$:
            .dw     shell_cmd_buf$
            add     hl,bc
            xor     a
            ld      (hl),a

            .db     0xcd
__reloc_dispatch_normalize$:
            .dw     shell_normalize$
            .db     0x3a
__reloc_dispatch_len2$:
            .dw     shell_cmd_len$
            or      a
            jr      z,shell_prompt$

            .db     0xcd
__reloc_dispatch_is_exit$:
            .dw     shell_is_exit$
            or      a
            jr      z,shell_dispatch_run$
            .db     0xcd
__reloc_dispatch_exit$:
            .dw     shell_exit_process$
            jr      shell_halt$

shell_dispatch_run$:
            .db     0xcd
__reloc_dispatch_run$:
            .dw     shell_run_command$
            ld      a,d
            or      e
            jr      z,shell_dispatch_err2$
            .db     0xcd
__reloc_dispatch_wait$:
            .dw     shell_wait_process$
            jp      shell_prompt$

shell_dispatch_err2$:
            .db     0xcd
__reloc_dispatch_err$:
            .dw     shell_write_error$
            jp      shell_prompt$

shell_init$:
            .db     0x21
__reloc_init_name$:
            .dw     shell_service_name$
            rst     0x10
            .db     0xed, 0x53
__reloc_init_store_partos$:
            .dw     shell_partos$
            ld      a,d
            or      e
            ret     z
            ld      bc,#PARTOS_OFF_REGISTER_SERVICE
            .db     0xcd
__reloc_regsvc_off$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,shell_init_done$
            .db     0x22
__reloc_regsvc_store_tmp$:
            .dw     shell_call_saved_target$
            .db     0x21
__reloc_init_shell_name$:
            .dw     shell_libc_service_name$
            .db     0x11
__reloc_init_shell_table$:
            .dw     shell_service_table$
            .db     0xcd
__reloc_regsvc_call$:
            .dw     shell_call_saved$
            ld      a,d
            or      e
            jr      z,shell_init_store_service$
            push    de
            pop     hl
            push    hl
            ld      bc,#SVC_FNTABLE
            add     hl,bc
            .db     0x11
__reloc_init_shell_table_repair$:
            .dw     shell_service_table$
            ld      (hl),e
            inc     hl
            ld      (hl),d
            pop     hl
            push    hl
            ld      bc,#SVC_NAME
            add     hl,bc
            ld      (hl),#'l'
            inc     hl
            ld      (hl),#'i'
            inc     hl
            ld      (hl),#'b'
            inc     hl
            ld      (hl),#'c'
            inc     hl
            xor     a
            ld      (hl),a
            pop     de
shell_init_store_service$:
            .db     0xed, 0x53
__reloc_init_store_shellsvc$:
            .dw     shell_registered_service$
shell_init_done$:
            .db     0xed, 0x5b
__reloc_init_store_partos_reload$:
            .dw     shell_partos$
            ret

shell_call_offset$:
            .db     0x2a
__reloc_calloff_partos$:
            .dw     shell_partos$
            ld      a,h
            or      l
            ret     z
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ret

shell_call_hl$:
            jp      (hl)

shell_call_saved$:
            ;; register_service(name, table) needs hl = name and de = table,
            ;; so shell_init patches this immediate CALL target just before use.
            .db     0xcd
shell_call_saved_target$:
            .dw     0x0000
            ret

shell_get_sys_info$:
            ld      bc,#PARTOS_OFF_GET_SYS_INFO
            .db     0xcd
__reloc_gsi_off$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,sgsi_none$
            .db     0xcd
__reloc_gsi_call$:
            .dw     shell_call_hl$
            ret
sgsi_none$:
            ld      de,#0x0000
            ret

shell_get_user_heap$:
            .db     0xcd
__reloc_guh_sysinfo$:
            .dw     shell_get_sys_info$
            ld      a,d
            or      e
            jr      z,sguh_none$
            ex      de,hl
            ld      bc,#SYSINFO_USER_HEAP
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ret
sguh_none$:
            ld      de,#0x0000
            ret

shell_get_current_process$:
            .db     0xcd
__reloc_gcp_sysinfo$:
            .dw     shell_get_sys_info$
            ld      a,d
            or      e
            jr      z,sgcp_none$
            ex      de,hl
            ld      bc,#SYSINFO_CURRENT_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            jr      z,sgcp_none$
            ex      de,hl
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ret
sgcp_none$:
            ld      de,#0x0000
            ret

shell_get_command_line$:
            .db     0xcd
__reloc_gcl_process$:
            .dw     shell_get_current_process$
            ld      a,d
            or      e
            jr      z,sgcl_fallback$
            ex      de,hl
            ld      bc,#PROCESS_CMDLINE
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            ret     nz
sgcl_fallback$:
            .db     0x3a
__reloc_gcl_len$:
            .dw     shell_cmd_len$
            or      a
            jr      z,sgcl_empty$
            .db     0x11
__reloc_gcl_buf$:
            .dw     shell_cmd_buf$
            ret
sgcl_empty$:
            .db     0x11
__reloc_gcl_empty$:
            .dw     shell_empty_env$
            ret

shell_get_environment$:
            .db     0xcd
__reloc_genv_process$:
            .dw     shell_get_current_process$
            ld      a,d
            or      e
            jr      z,sgenv_empty$
            ex      de,hl
            ld      bc,#PROCESS_ENV
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            ret     nz
sgenv_empty$:
            .db     0x11
__reloc_genv_empty$:
            .dw     shell_empty_env$
            ret

shell_getenv$:
            ld      a,h
            or      l
            jr      z,sgev_fail$
            .db     0x22
__reloc_getenv_name_store$:
            .dw     shell_tmp_ptr$
            .db     0xcd
__reloc_getenv_env_call$:
            .dw     shell_get_environment$
            ex      de,hl
sgev_entry$:
            ld      a,(hl)
            or      a
            jr      z,sgev_fail$
            push    hl
            .db     0xed, 0x5b
__reloc_getenv_name_load$:
            .dw     shell_tmp_ptr$
sgev_cmp$:
            ld      a,(de)
            or      a
            jr      z,sgev_have_name$
            cp      (hl)
            jr      nz,sgev_skip$
            inc     de
            inc     hl
            jr      sgev_cmp$
sgev_have_name$:
            ld      a,(hl)
            cp      #'='
            jr      z,sgev_found$
sgev_skip$:
            pop     hl
sgev_skip_loop$:
            ld      a,(hl)
            or      a
            jr      z,sgev_next$
            inc     hl
            jr      sgev_skip_loop$
sgev_next$:
            inc     hl
            jr      sgev_entry$
sgev_found$:
            pop     bc
            inc     hl
            ex      de,hl
            ret
sgev_fail$:
            ld      de,#0x0000
            ret

shell_attach_launch$:
            ld      a,d
            or      e
            ret     z
            push    de
            ex      de,hl
            ld      bc,#PROCESS_ENV
            add     hl,bc
            .db     0x11
__reloc_attach_empty_env$:
            .dw     shell_empty_env$
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ld      bc,#PARTOS_OFF_ALLOCATE_MEMORY
            .db     0xcd
__reloc_attach_alloc_off$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,sal_store_empty$
            .db     0x22
__reloc_attach_alloc_store$:
            .dw     shell_tmp_ptr$
            .db     0xcd
__reloc_attach_heap_call$:
            .dw     shell_get_user_heap$
            ld      a,d
            or      e
            jr      z,sal_store_empty$
            ex      de,hl
            .db     0x3a
__reloc_attach_len$:
            .dw     shell_cmd_len$
            ld      e,a
            ld      d,#0
            inc     de
            ld      bc,#sal_alloc_ret$
            push    bc
            .db     0xed, 0x4b
__reloc_attach_alloc_load$:
            .dw     shell_tmp_ptr$
            push    bc
            ret
sal_alloc_ret$:
            pop     bc
            ld      a,d
            or      e
            jr      z,sal_store_empty_bc$
            push    bc
            push    de
            ex      de,hl
            .db     0x11
__reloc_attach_src$:
            .dw     shell_cmd_buf$
            .db     0x3a
__reloc_attach_len2$:
            .dw     shell_cmd_len$
            ld      c,a
            ld      b,#0
            inc     bc
            ex      de,hl
            ldir
            pop     de
            pop     hl
            ld      b,h
            ld      c,l                 ; bc = process pointer for the return
            ld      hl,#PROCESS_CMDLINE
            add     hl,bc
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ld      d,b
            ld      e,c
            ret
sal_store_empty$:
            pop     bc
sal_store_empty_bc$:
            push    bc
            pop     hl
            push    hl                  ; keep the real process pointer for ret
            ld      bc,#PROCESS_CMDLINE
            add     hl,bc
            .db     0x11
__reloc_attach_empty_cmd$:
            .dw     shell_empty_env$
            ld      (hl),e
            inc     hl
            ld      (hl),d
            pop     de
            ret

shell_clear$:
            ld      bc,#PARTOS_OFF_CLEAR_SCREEN
            .db     0xcd
__reloc_clear_off$:
            .dw     shell_call_offset$
            .db     0xcd
__reloc_clear_call$:
            .dw     shell_call_hl$
            ret

shell_read_key$:
            ld      bc,#PARTOS_OFF_READ_KEYBOARD
            .db     0xcd
__reloc_rk_off$:
            .dw     shell_call_offset$
            .db     0xcd
__reloc_rk_call$:
            .dw     shell_call_hl$
            ret

shell_get_current_dev$:
            ld      bc,#PARTOS_OFF_GET_BOOT_FS
            .db     0xcd
__reloc_gd_off$:
            .dw     shell_call_offset$
            .db     0xcd
__reloc_gd_call$:
            .dw     shell_call_hl$
            ld      a,d
            or      e
            jr      z,sgd_none$
            ex      de,hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            jr      z,sgd_none$
            ex      de,hl
            ld      de,#2
            add     hl,de
            ex      de,hl
            ret
sgd_none$:
            ld      de,#0x0000
            ret

shell_get_device_alias$:
            .db     0xcd
__reloc_alias_dev$:
            .dw     shell_get_current_dev$
            ld      a,d
            or      e
            ret     z
            ex      de,hl
            ld      a,(hl)
            cp      #'s'
            jr      nz,sgda_raw$
            inc     hl
            ld      a,(hl)
            cp      #'d'
            jr      nz,sgda_raw$
            inc     hl
            ld      a,(hl)
            cp      #'a'
            jr      z,sgda_hd0$
            cp      #'b'
            jr      z,sgda_hd1$
sgda_raw$:
            ex      de,hl
            ret
sgda_hd0$:
            .db     0x11
__reloc_alias_hd0$:
            .dw     shell_hd0$
            ret
sgda_hd1$:
            .db     0x11
__reloc_alias_hd1$:
            .dw     shell_hd1$
            ret

shell_skip_spaces$:
sss_loop$:
            ld      a,(hl)
            cp      #' '
            ret     nz
            inc     hl
            jr      sss_loop$

shell_normalize$:
            .db     0x21
__reloc_norm_buf_in$:
            .dw     shell_cmd_buf$
            .db     0xcd
__reloc_norm_skip$:
            .dw     shell_skip_spaces$
            .db     0x11
__reloc_norm_buf_out$:
            .dw     shell_cmd_buf$
            ld      b,#0
sn_copy$:
            ld      a,(hl)
            or      a
            jr      z,sn_copy_done$
            ld      (de),a
            inc     de
            inc     hl
            inc     b
            jr      sn_copy$
sn_copy_done$:
            xor     a
            ld      (de),a
            ld      a,b
            .db     0x32
__reloc_norm_len_store$:
            .dw     shell_cmd_len$
sn_trim$:
            .db     0x3a
__reloc_norm_len_load$:
            .dw     shell_cmd_len$
            or      a
            ret     z
            dec     a
            ld      c,a
            ld      b,#0
            .db     0x21
__reloc_norm_trim_buf$:
            .dw     shell_cmd_buf$
            add     hl,bc
            ld      a,(hl)
            cp      #' '
            ret     nz
            xor     a
            ld      (hl),a
            ld      a,c
            .db     0x32
__reloc_norm_trim_store$:
            .dw     shell_cmd_len$
            jr      sn_trim$

shell_upper$:
            cp      #'a'
            ret     c
            cp      #('z' + 1)
            ret     nc
            sub     #0x20
            ret

shell_is_exit$:
            .db     0x3a
__reloc_exit_len$:
            .dw     shell_cmd_len$
            cp      #4
            jr      nz,sie_no$
            .db     0x21
__reloc_exit_buf$:
            .dw     shell_cmd_buf$
            ld      a,(hl)
            .db     0xcd
__reloc_exit_upper0$:
            .dw     shell_upper$
            cp      #'E'
            jr      nz,sie_no$
            inc     hl
            ld      a,(hl)
            .db     0xcd
__reloc_exit_upper1$:
            .dw     shell_upper$
            cp      #'X'
            jr      nz,sie_no$
            inc     hl
            ld      a,(hl)
            .db     0xcd
__reloc_exit_upper2$:
            .dw     shell_upper$
            cp      #'I'
            jr      nz,sie_no$
            inc     hl
            ld      a,(hl)
            .db     0xcd
__reloc_exit_upper3$:
            .dw     shell_upper$
            cp      #'T'
            jr      nz,sie_no$
            ld      a,#1
            ret
sie_no$:
            xor     a
            ret

shell_exit_process$:
            ld      bc,#PARTOS_OFF_EXIT_PROCESS
            .db     0xcd
__reloc_exit_calloff$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            ret     z
            jp      (hl)

shell_run_command$:
            ld      bc,#PARTOS_OFF_RUN_COMMAND
            .db     0xcd
__reloc_run_off$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,src_fail$
            .db     0x22
__reloc_run_store_tmp$:
            .dw     shell_tmp_ptr$
            .db     0x21
__reloc_run_buf$:
            .dw     shell_cmd_buf$
            ld      bc,#src_after_run$
            push    bc
            .db     0xed, 0x5b
__reloc_run_load_tmp$:
            .dw     shell_tmp_ptr$
            push    de
            ret
src_after_run$:
            .db     0xcd
__reloc_run_attach$:
            .dw     shell_attach_launch$
            .db     0xed, 0x53
__reloc_run_result_store$:
            .dw     shell_run_result_debug$
            ret
src_fail$:
            ld      de,#0x0000
            ret

shell_wait_process$:
            ld      (shell_wait_input_debug$),de
            ld      a,d
            or      e
            ret     z
            .db     0xed, 0x53
__reloc_wait_store_tmp$:
            .dw     shell_tmp_ptr$
            ld      bc,#PARTOS_OFF_WAIT_PROCESS
            .db     0xcd
__reloc_wait_calloff$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,swp_fail$
            push    hl                  ; save resolved _process_wait entry
            .db     0x2a
__reloc_wait_load_tmp$:
            .dw     shell_tmp_ptr$
            push    hl                  ; duplicate process pointer so DE
            pop     de                  ; carries the same target as HL
            pop     bc                  ; bc = resolved _process_wait entry
            push    bc
            ret
swp_fail$:
            ld      de,#0x0000
            ret

shell_write_buffer$:
            push    hl                  ; preserve the real (hl,de) arguments
            push    de                  ; across the service-table lookup
            ld      bc,#PARTOS_OFF_WRITE_CONSOLE
            .db     0xcd
__reloc_wb_off$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            jr      z,swb_fail$
            ex      de,hl               ; keep the resolved service entry while
                                        ; the original arguments come back off
                                        ; the stack
            pop     bc                  ; bc = original len
            pop     hl                  ; hl = original ptr
            push    de                  ; tail-call the resolved service
            ld      d,b
            ld      e,c
            ret
swb_fail$:
            pop     de
            pop     hl
            ret

shell_echo_char$:
            .db     0x32
__reloc_echo_store$:
            .dw     shell_char$
            .db     0x21
__reloc_echo_ptr$:
            .dw     shell_char$
            ld      de,#1
            .db     0xcd
__reloc_echo_write$:
            .dw     shell_write_buffer$
            ret

shell_write_banner$:
            .db     0x21
__reloc_banner_ptr$:
            .dw     shell_banner$
            ld      de,#shell_banner_len
            .db     0xcd
__reloc_banner_write$:
            .dw     shell_write_buffer$
            ret

shell_write_prompt$:
            .db     0xcd
__reloc_prompt_dev$:
            .dw     shell_get_device_alias$
            ld      a,d
            or      e
            jr      z,swp_suffix$
            ex      de,hl
            .db     0xcd
__reloc_prompt_dev_write$:
            .dw     shell_write_cstr$
swp_suffix$:
            .db     0x21
__reloc_prompt_ptr2$:
            .dw     shell_prompt_text$
            ld      de,#shell_prompt_len
            .db     0xcd
__reloc_prompt_write2$:
            .dw     shell_write_buffer$
            ret

shell_write_cstr$:
            push    hl
            ld      de,#0
swc_len$:
            ld      a,(hl)
            or      a
            jr      z,swc_go$
            inc     hl
            inc     de
            jr      swc_len$
swc_go$:
            pop     hl
            .db     0xcd
__reloc_wcs_write$:
            .dw     shell_write_buffer$
            ret

shell_write_newline$:
            .db     0x21
__reloc_nl_ptr$:
            .dw     shell_newline$
            ld      de,#shell_newline_len
            .db     0xcd
__reloc_nl_write$:
            .dw     shell_write_buffer$
            ret

shell_write_erase$:
            .db     0x21
__reloc_erase_ptr$:
            .dw     shell_erase_seq$
            ld      de,#shell_erase_seq_len
            .db     0xcd
__reloc_erase_write$:
            .dw     shell_write_buffer$
            ret

shell_write_error$:
            .db     0x21
__reloc_err_ptr$:
            .dw     shell_error$
            ld      de,#shell_error_len
            .db     0xcd
__reloc_err_write2$:
            .dw     shell_write_buffer$
            ret

shell_service_name$:
            .db     'p','a','r','t','o','s',0

shell_libc_service_name$:
            .db     'l','i','b','c',0

shell_service_table$:
__reloc_libcsvc_getcmd$:
            .dw     shell_get_command_line$
__reloc_libcsvc_getenv$:
            .dw     shell_get_environment$
__reloc_libcsvc_getenvvar$:
            .dw     shell_getenv$
__reloc_libcsvc_getdev$:
            .dw     shell_get_device_alias$
__reloc_libcsvc_prompt$:
            .dw     shell_write_prompt$

shell_banner$:
            .db     'P','A','R','T','O','S',' ','s','h','e','l','l',0x0d,0x0a
shell_banner_len == . - shell_banner$

shell_prompt_text$:
            .db     '>',' '
shell_prompt_len == . - shell_prompt_text$

shell_hd0$:
            .db     'h','d','0',0

shell_hd1$:
            .db     'h','d','1',0

shell_newline$:
            .db     0x0d,0x0a
shell_newline_len == . - shell_newline$

shell_erase_seq$:
            .db     0x08,' ',0x08
shell_erase_seq_len == . - shell_erase_seq$

shell_error$:
            .db     '?',0x0d,0x0a
shell_error_len == . - shell_error$

shell_empty_env$:
            .db     0x00,0x00

shell_partos$:
            .dw     0x0000
shell_registered_service$:
            .dw     0x0000
shell_tmp_ptr$:
            .dw     0x0000
shell_run_result_debug$:
            .dw     0x0000
shell_wait_input_debug$:
            .dw     0x0000
shell_cmd_len$:
            .db     0x00
shell_char$:
            .db     0x00
shell_cmd_buf$:
            .ds     64

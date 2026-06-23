            ;; shell.s
            ;;
            ;; first PartOS shell payload.
            ;;
            ;; this is a flat payload linked at address 0. tools/makecom.py
            ;; scans every symbol whose name starts with "__reloc_" and turns
            ;; those word locations into XL relocation entries before it wraps
            ;; the image in a COM header.
            ;;
            ;; the shell itself uses only the public "partos" service:
            ;;   - clear_screen
            ;;   - write_console
            ;;   - read_keyboard
            ;;   - run_command
            ;;
            ;; 2026-06-22   tstih
            .module shell

            .equ    PARTOS_OFF_CLEAR_SCREEN,    2
            .equ    PARTOS_OFF_WRITE_CONSOLE,   6
            .equ    PARTOS_OFF_READ_KEYBOARD,   10
            .equ    PARTOS_OFF_RUN_COMMAND,     12
            .equ    PARTOS_OFF_EXIT_PROCESS,    70
            .equ    PARTOS_OFF_WAIT_PROCESS,    98

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
            .globl  __reloc_calloff_partos$
            .globl  __reloc_clear_off$
            .globl  __reloc_clear_call$
            .globl  __reloc_rk_off$
            .globl  __reloc_rk_call$
            .globl  __reloc_run_buf$
            .globl  __reloc_run_store_tmp$
            .globl  __reloc_run_off$
            .globl  __reloc_run_load_tmp$
            .globl  __reloc_wait_calloff$
            .globl  __reloc_wb_off$
            .globl  __reloc_echo_store$
            .globl  __reloc_echo_ptr$
            .globl  __reloc_echo_write$
            .globl  __reloc_banner_ptr$
            .globl  __reloc_banner_write$
            .globl  __reloc_prompt_ptr2$
            .globl  __reloc_prompt_write2$
            .globl  __reloc_nl_ptr$
            .globl  __reloc_nl_write$
            .globl  __reloc_err_ptr$
            .globl  __reloc_err_write2$
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
            .globl  __reloc_exit_upper0$
            .globl  __reloc_exit_upper1$
            .globl  __reloc_exit_upper2$
            .globl  __reloc_exit_upper3$
            .globl  __reloc_exit_calloff$

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
            jr      shell_prompt$

shell_dispatch_err2$:
            .db     0xcd
__reloc_dispatch_err$:
            .dw     shell_write_error$
            jr      shell_prompt$

shell_init$:
            .db     0x21
__reloc_init_name$:
            .dw     shell_service_name$
            rst     0x10
            .db     0xed, 0x53
__reloc_init_store_partos$:
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
            .db     0x21
__reloc_run_buf$:
            .dw     shell_cmd_buf$
            .db     0x22
__reloc_run_store_tmp$:
            .dw     shell_tmp_ptr$
            ld      bc,#PARTOS_OFF_RUN_COMMAND
            .db     0xcd
__reloc_run_off$:
            .dw     shell_call_offset$
            push    hl                  ; tail-call the resolved service after
                                        ; restoring the command-buffer pointer
            .db     0x2a
__reloc_run_load_tmp$:
            .dw     shell_tmp_ptr$
            ret

shell_wait_process$:
            ex      de,hl
            ld      bc,#PARTOS_OFF_WAIT_PROCESS
            .db     0xcd
__reloc_wait_calloff$:
            .dw     shell_call_offset$
            ld      a,h
            or      l
            ret     z
            jp      (hl)

shell_write_buffer$:
            push    hl                  ; preserve the real (hl,de) arguments
            push    de                  ; across the service-table lookup
            ld      bc,#PARTOS_OFF_WRITE_CONSOLE
            .db     0xcd
__reloc_wb_off$:
            .dw     shell_call_offset$
            ex      de,hl               ; keep the resolved service entry while
                                        ; the original arguments come back off
                                        ; the stack
            pop     bc                  ; bc = original len
            pop     hl                  ; hl = original ptr
            push    de                  ; tail-call the resolved service
            ld      d,b
            ld      e,c
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
            .db     0x21
__reloc_prompt_ptr2$:
            .dw     shell_prompt_text$
            ld      de,#shell_prompt_len
            .db     0xcd
__reloc_prompt_write2$:
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

shell_banner$:
            .db     'P','A','R','T','O','S',' ','s','h','e','l','l',0x0d,0x0a
shell_banner_len == . - shell_banner$

shell_prompt_text$:
            .db     '>',' '
shell_prompt_len == . - shell_prompt_text$

shell_newline$:
            .db     0x0d,0x0a
shell_newline_len == . - shell_newline$

shell_error$:
            .db     '?',0x0d,0x0a
shell_error_len == . - shell_error$

shell_partos$:
            .dw     0x0000
shell_tmp_ptr$:
            .dw     0x0000
shell_cmd_len$:
            .db     0x00
shell_char$:
            .db     0x00
shell_cmd_buf$:
            .ds     64

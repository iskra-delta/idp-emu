            ;; partos.s
            ;;
            ;; tiny userland helper layer shared by the PartOS .COM tools.
            ;; every helper talks only through the public "partos" service.
            ;;
            ;; 2026-06-23   tstih
            .module partos_app

            .equ    PARTOS_OFF_GET_SYS_INFO,    0
            .equ    PARTOS_OFF_CLEAR_SCREEN,    2
            .equ    PARTOS_OFF_WRITE_CONSOLE,   6
            .equ    PARTOS_OFF_CREATE_EVENT,    44
            .equ    PARTOS_OFF_DESTROY_EVENT,   46
            .equ    PARTOS_OFF_WAIT_EVENTS,     62
            .equ    PARTOS_OFF_EXIT_PROCESS,    70
            .equ    PARTOS_OFF_LOOKUP_PATH,     76
            .equ    PARTOS_OFF_OPEN_FILE,       78
            .equ    PARTOS_OFF_CREATE_FILE,     80
            .equ    PARTOS_OFF_READ_FILE,       82
            .equ    PARTOS_OFF_WRITE_FILE,      84
            .equ    PARTOS_OFF_READDIR,         86
            .equ    PARTOS_OFF_GET_BOOT_FS,     88
            .equ    PARTOS_OFF_GET_CMDLINE,     90
            .equ    PARTOS_OFF_UNLINK_PATH,     92
            .equ    PARTOS_OFF_MKDIR_PATH,      94
            .equ    PARTOS_OFF_RMDIR_PATH,      96
            .equ    PARTOS_OFF_WAIT_PROCESS,    98

            .globl  pa_init$
            .globl  pa_call_offset$
            .globl  pa_call_fn$
            .globl  pa_clear_screen$
            .globl  pa_status_at$
            .globl  pa_write_buffer$
            .globl  pa_write_cstr$
            .globl  pa_write_char$
            .globl  pa_write_newline$
            .globl  pa_write_hex16$
            .globl  pa_create_event$
            .globl  pa_destroy_event$
            .globl  pa_wait_one$
            .globl  pa_get_sys_info$
            .globl  pa_get_boot_fs$
            .globl  pa_get_command_line$
            .globl  pa_exit_process$
            .globl  pa_wait_process$
            .globl  pa_lookup_path$
            .globl  pa_open_file$
            .globl  pa_create_file$
            .globl  pa_read_file$
            .globl  pa_write_file$
            .globl  pa_readdir$
            .globl  pa_unlink_path$
            .globl  pa_mkdir_path$
            .globl  pa_rmdir_path$
            .globl  pa_skip_spaces$
            .globl  pa_arg_start$
            .globl  pa_copy_token$
            .globl  pa_require_eol$

            .area   _CODE

pa_init$:
            ld      hl,#pa_service_name$
            rst     0x10
            ld      (pa_partos$),de
            ret

pa_clear_screen$:
            ld      bc,#PARTOS_OFF_CLEAR_SCREEN
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            jp      (hl)

pa_call_offset$:
            ld      hl,(pa_partos$)
            ld      a,h
            or      l
            ret     z
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ret

pa_call_fn$:
            ld      bc,(pa_fn$)
            push    bc
            ret

pa_status_at$:
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ret

pa_write_buffer$:
            push    hl
            push    de
            ld      bc,#PARTOS_OFF_WRITE_CONSOLE
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pwb_fail$
            ld      (pa_fn$),hl
            pop     de
            pop     hl
            call    pa_call_fn$
            ret
pwb_fail$:
            pop     de
            pop     hl
            ret

pa_write_cstr$:
            push    hl
            ld      de,#0
pwcs_len$:
            ld      a,(hl)
            or      a
            jr      z,pwcs_go$
            inc     hl
            inc     de
            jr      pwcs_len$
pwcs_go$:
            pop     hl
            jp      pa_write_buffer$

pa_write_char$:
            ld      (pa_char$),a
            ld      hl,#pa_char$
            ld      de,#1
            jp      pa_write_buffer$

pa_write_newline$:
            ld      hl,#pa_newline$
            ld      de,#2
            jp      pa_write_buffer$

pa_hex_nibble$:
            and     #0x0f
            add     a,#'0'
            cp      #('9' + 1)
            ret     c
            add     a,#7
            ret

pa_hex_byte_pair$:
            push    af
            rrca
            rrca
            rrca
            rrca
            call    pa_hex_nibble$
            ld      (de),a
            inc     de
            pop     af
            call    pa_hex_nibble$
            ld      (de),a
            inc     de
            ret

pa_write_hex16$:
            ld      de,#pa_hexbuf$
            ld      a,#'0'
            ld      (de),a
            inc     de
            ld      a,#'x'
            ld      (de),a
            inc     de
            ld      a,h
            call    pa_hex_byte_pair$
            ld      a,l
            call    pa_hex_byte_pair$
            ld      hl,#pa_hexbuf$
            ld      de,#6
            jp      pa_write_buffer$

pa_create_event$:
            ld      bc,#PARTOS_OFF_CREATE_EVENT
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_ce_fail$
            ld      (pa_fn$),hl
            ld      hl,#0x0000
            call    pa_call_fn$
            ld      (pa_event$),de
            ld      hl,#pa_wait_events$
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ret
pa_ce_fail$:
            ld      de,#0x0000
            ret

pa_destroy_event$:
            ld      hl,(pa_event$)
            ld      a,h
            or      l
            ret     z
            push    hl
            ld      bc,#PARTOS_OFF_DESTROY_EVENT
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pde_fail$
            ld      (pa_fn$),hl
            pop     hl
            call    pa_call_fn$
            xor     a
            ld      (pa_event$),a
            ld      (pa_event$ + 1),a
            ld      (pa_wait_events$),a
            ld      (pa_wait_events$ + 1),a
            ret
pde_fail$:
            pop     hl
            ret

pa_wait_one$:
            ld      bc,#PARTOS_OFF_WAIT_EVENTS
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (pa_fn$),hl
            ld      hl,#pa_wait_events$
            ld      a,#1
            push    af
            inc     sp
            call    pa_call_fn$
            ret

pa_get_sys_info$:
            ld      bc,#PARTOS_OFF_GET_SYS_INFO
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (pa_fn$),hl
            call    pa_call_fn$
            ret

pa_get_boot_fs$:
            ld      bc,#PARTOS_OFF_GET_BOOT_FS
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (pa_fn$),hl
            call    pa_call_fn$
            ret

pa_get_command_line$:
            ld      bc,#PARTOS_OFF_GET_CMDLINE
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (pa_fn$),hl
            call    pa_call_fn$
            ret

pa_exit_process$:
            ld      bc,#PARTOS_OFF_EXIT_PROCESS
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            jp      (hl)

pa_wait_process$:
            ld      bc,#PARTOS_OFF_WAIT_PROCESS
            call    pa_call_offset$
            ld      a,h
            or      l
            ret     z
            jp      (hl)

pa_lookup_path$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_LOOKUP_PATH
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_lkp_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; result
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_lkp_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_open_file$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_OPEN_FILE
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_open_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; file
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_open_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_create_file$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_CREATE_FILE
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_create_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; file
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_create_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_read_file$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_READ_FILE
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_read_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            ld      (pa_tmp_word$),bc
            ld      bc,(pa_event$)
            push    bc                  ; event
            ld      bc,(pa_tmp_word$)
            push    bc                  ; bytes
            call    pa_call_fn$
            ret
pa_read_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_write_file$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_WRITE_FILE
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_write_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            ld      (pa_tmp_word$),bc
            ld      bc,(pa_event$)
            push    bc                  ; event
            ld      bc,(pa_tmp_word$)
            push    bc                  ; bytes
            call    pa_call_fn$
            ret
pa_write_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_readdir$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_READDIR
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_readdir_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; info
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_readdir_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_unlink_path$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_UNLINK_PATH
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_unlink_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; result
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_unlink_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_mkdir_path$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_MKDIR_PATH
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_mkdir_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; result
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_mkdir_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_rmdir_path$:
            push    hl
            push    de
            push    bc
            ld      bc,#PARTOS_OFF_RMDIR_PATH
            call    pa_call_offset$
            ld      a,h
            or      l
            jr      z,pa_rmdir_fail$
            ld      (pa_fn$),hl
            pop     bc
            pop     de
            pop     hl
            push    bc                  ; result
            ld      bc,(pa_event$)
            push    bc                  ; event
            call    pa_call_fn$
            ret
pa_rmdir_fail$:
            pop     bc
            pop     de
            pop     hl
            ld      de,#0x0000
            ret

pa_skip_spaces$:
pss_loop$:
            ld      a,(hl)
            cp      #' '
            ret     nz
            inc     hl
            jr      pss_loop$

pa_arg_start$:
            call    pa_get_command_line$
            ex      de,hl
            call    pa_skip_spaces$
pas_skip0$:
            ld      a,(hl)
            or      a
            ret     z
            cp      #' '
            jr      z,pas_gap$
            inc     hl
            jr      pas_skip0$
pas_gap$:
            call    pa_skip_spaces$
            ret

pa_copy_token$:
            call    pa_skip_spaces$
            ld      a,(hl)
            or      a
            jr      z,pct_fail$
            ld      c,b
pct_loop$:
            ld      a,(hl)
            or      a
            jr      z,pct_done$
            cp      #' '
            jr      z,pct_done$
            ld      a,c
            or      a
            jr      z,pct_fail$
            ld      a,(hl)
            ld      (de),a
            inc     de
            inc     hl
            dec     c
            jr      pct_loop$
pct_done$:
            xor     a
            ld      (de),a
            or      a
            ret
pct_fail$:
            xor     a
            ld      (de),a
            scf
            ret

pa_require_eol$:
            call    pa_skip_spaces$
            ld      a,(hl)
            or      a
            ret

pa_service_name$:
            .db     'p','a','r','t','o','s',0

pa_newline$:
            .db     0x0d,0x0a

            .area   _INITIALIZED

pa_partos$:
            .dw     0x0000
pa_fn$:
            .dw     0x0000
pa_event$:
            .dw     0x0000
pa_wait_events$:
            .dw     0x0000
pa_tmp_word$:
            .dw     0x0000
pa_char$:
            .db     0x00
pa_hexbuf$:
            .db     0,0,0,0,0,0

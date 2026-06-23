            ;; ps.s
            ;;
            ;; minimal process viewer for PartOS userland.
            ;;
            ;; it stays entirely on the public "partos" service surface:
            ;;   - query the syscall table via rst 0x10
            ;;   - fetch sys_info
            ;;   - walk the live process list
            ;;   - print one line per process with its main-thread state
            ;;
            ;; 2026-06-23   tstih
            .module ps

            .equ    PARTOS_OFF_GET_SYS_INFO,   0
            .equ    PARTOS_OFF_WRITE_CONSOLE,  6
            .equ    PARTOS_OFF_EXIT_PROCESS,   70

            .equ    SYSINFO_FIRST_PROCESS,     18
            .equ    SYSINFO_CURRENT_THREAD,    24

            .equ    PROCESS_PNAME,             5
            .equ    PROCESS_MAIN_THREAD,       13

            .equ    THREAD_STATE,              19
            .equ    THREAD_PROCESS,            22
            .equ    THREAD_BANK,               24
            .equ    THREAD_STATE_SUSPENDED,    0
            .equ    THREAD_STATE_RUNNING,      1
            .equ    THREAD_STATE_WAITING,      2
            .equ    THREAD_STATE_JOINED,       3
            .equ    THREAD_STATE_TERMINATED,   4

            .area   _CODE

ps_entry::
            call    ps_init$
            ld      a,d
            or      e
            jr      nz,ps_ready$

ps_dead$:
            halt
            jr      ps_dead$

ps_ready$:
            call    ps_run$
            call    ps_exit_process$
            jr      ps_dead$

ps_init$:
            ld      hl,#ps_service_name$
            rst     0x10
            ld      (ps_partos$),de
            ret

ps_run$:
            call    ps_get_sys_info$
            ld      a,d
            or      e
            ret     z

            ex      de,hl
            ld      (ps_info$),hl

            ld      bc,#SYSINFO_CURRENT_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            xor     a
            ld      (ps_current_process$),a
            ld      (ps_current_process$ + 1),a
            ld      a,d
            or      e
            jr      z,ps_have_current$
            ex      de,hl
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (ps_current_process$),de

ps_have_current$:
            ld      hl,(ps_info$)
            ld      bc,#SYSINFO_FIRST_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)

ps_loop$:
            ld      a,d
            or      e
            ret     z
            push    de
            call    ps_write_process_line$
            pop     de
            ex      de,hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            jr      ps_loop$

ps_get_sys_info$:
            ld      bc,#PARTOS_OFF_GET_SYS_INFO
            call    ps_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (ps_fn$),hl
            call    ps_call_fn$
            ret

ps_exit_process$:
            ld      bc,#PARTOS_OFF_EXIT_PROCESS
            call    ps_call_offset$
            ld      a,h
            or      l
            ret     z
            ld      (ps_fn$),hl
            call    ps_call_fn$
            ret

ps_call_offset$:
            ld      hl,(ps_partos$)
            ld      a,h
            or      l
            ret     z
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ret

ps_call_fn$:
            ld      bc,(ps_fn$)
            push    bc
            ret

ps_write_buffer$:
            push    hl
            push    de
            ld      bc,#PARTOS_OFF_WRITE_CONSOLE
            call    ps_call_offset$
            ld      (ps_fn$),hl
            pop     de
            pop     hl
            call    ps_call_fn$
            ret

ps_write_char$:
            ld      (ps_char$),a
            ld      hl,#ps_char$
            ld      de,#1
            call    ps_write_buffer$
            ret

ps_write_newline$:
            ld      hl,#ps_newline$
            ld      de,#ps_newline_len
            call    ps_write_buffer$
            ret

ps_write_name$:
            push    de
            ex      de,hl
            ld      bc,#PROCESS_PNAME
            add     hl,bc
            push    hl
            ld      de,#0
            ld      b,#8
psn_scan$:
            ld      a,b
            or      a
            jr      z,psn_done$
            ld      a,(hl)
            or      a
            jr      z,psn_done$
            inc     hl
            inc     e
            djnz    psn_scan$
psn_done$:
            pop     hl
            call    ps_write_buffer$
            pop     de
            ret

ps_write_state$:
            push    de
            ex      de,hl
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            jr      z,pss_unknown$
            ex      de,hl
            ld      bc,#THREAD_STATE
            add     hl,bc
            ld      a,(hl)
            cp      #THREAD_STATE_SUSPENDED
            jr      z,pss_suspended$
            cp      #THREAD_STATE_RUNNING
            jr      z,pss_running$
            cp      #THREAD_STATE_WAITING
            jr      z,pss_waiting$
            cp      #THREAD_STATE_JOINED
            jr      z,pss_joined$
            cp      #THREAD_STATE_TERMINATED
            jr      z,pss_terminated$

pss_unknown$:
            ld      a,#'?'
            jr      pss_emit$
pss_suspended$:
            ld      a,#'S'
            jr      pss_emit$
pss_running$:
            ld      a,#'R'
            jr      pss_emit$
pss_waiting$:
            ld      a,#'W'
            jr      pss_emit$
pss_joined$:
            ld      a,#'J'
            jr      pss_emit$
pss_terminated$:
            ld      a,#'T'

pss_emit$:
            call    ps_write_char$
            pop     de
            ret

ps_write_bank$:
            push    de
            ex      de,hl
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            jr      z,psb_unknown$
            ex      de,hl
            ld      bc,#THREAD_BANK
            add     hl,bc
            ld      a,(hl)
            add     a,#'0'
            jr      psb_emit$
psb_unknown$:
            ld      a,#'?'
psb_emit$:
            call    ps_write_char$
            pop     de
            ret

ps_write_process_line$:
            push    de
            ld      hl,(ps_current_process$)
            ld      a,e
            cp      l
            jr      nz,psw_other$
            ld      a,d
            cp      h
            jr      nz,psw_other$
            ld      a,#'*'
            jr      psw_marked$
psw_other$:
            ld      a,#' '
psw_marked$:
            call    ps_write_char$
            ld      a,#' '
            call    ps_write_char$
            pop     de
            push    de
            call    ps_write_name$
            ld      a,#' '
            call    ps_write_char$
            ld      a,#'['
            call    ps_write_char$
            pop     de
            push    de
            call    ps_write_state$
            ld      a,#' '
            call    ps_write_char$
            ld      a,#'b'
            call    ps_write_char$
            pop     de
            push    de
            call    ps_write_bank$
            ld      a,#']'
            call    ps_write_char$
            call    ps_write_newline$
            pop     de
            ret

ps_service_name$:
            .db     'p','a','r','t','o','s',0

ps_newline$:
            .db     0x0d,0x0a
ps_newline_len == . - ps_newline$

ps_partos$:
            .dw     0x0000
ps_fn$:
            .dw     0x0000
ps_info$:
            .dw     0x0000
ps_current_process$:
            .dw     0x0000
ps_char$:
            .db     0x00

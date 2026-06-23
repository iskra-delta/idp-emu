            ;; mem.s
            ;;
            ;; current-bank heap summary for PartOS userland.
            ;;
            ;; 2026-06-23   tstih
            .module mem

            .globl  pa_init$
            .globl  pa_get_sys_info$
            .globl  pa_write_cstr$
            .globl  pa_write_hex16$
            .globl  pa_write_newline$
            .globl  pa_exit_process$

            .equ    SYSINFO_SYSTEM_HEAP,        34
            .equ    SYSINFO_USER_HEAP,          36
            .equ    BLOCK_STAT,                 4
            .equ    BLOCK_SIZE,                 5
            .equ    ALLOCATED,                  0x01

            .area   _CODE

mem_entry::
            call    pa_init$
            ld      a,d
            or      e
            jr      nz,mem_ready$
mem_dead$:
            halt
            jr      mem_dead$

mem_ready$:
            call    pa_get_sys_info$
            ld      a,d
            or      e
            jr      z,mem_exit$
            ex      de,hl

            push    hl
            ld      bc,#SYSINFO_SYSTEM_HEAP
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            call    mem_scan_heap$
            ld      hl,#mem_sys_text$
            call    pa_write_cstr$
            ld      hl,(mem_used$)
            call    pa_write_hex16$
            ld      hl,#mem_free_text$
            call    pa_write_cstr$
            ld      hl,(mem_free$)
            call    pa_write_hex16$
            call    pa_write_newline$
            pop     hl

            ld      bc,#SYSINFO_USER_HEAP
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            call    mem_scan_heap$
            ld      hl,#mem_usr_text$
            call    pa_write_cstr$
            ld      hl,(mem_used$)
            call    pa_write_hex16$
            ld      hl,#mem_free_text$
            call    pa_write_cstr$
            ld      hl,(mem_free$)
            call    pa_write_hex16$
            call    pa_write_newline$

mem_exit$:
            call    pa_exit_process$
            jr      mem_dead$

mem_scan_heap$:
            xor     a
            ld      (mem_used$),a
            ld      (mem_used$ + 1),a
            ld      (mem_free$),a
            ld      (mem_free$ + 1),a
            ld      a,h
            or      l
            ret     z

msh_loop$:
            push    hl
            ld      bc,#BLOCK_STAT
            add     hl,bc
            ld      a,(hl)
            and     #ALLOCATED
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     hl
            jr      z,msh_free$
            push    hl
            ld      hl,(mem_used$)
            add     hl,de
            ld      (mem_used$),hl
            pop     hl
            jr      msh_next$
msh_free$:
            push    hl
            ld      hl,(mem_free$)
            add     hl,de
            ld      (mem_free$),hl
            pop     hl
msh_next$:
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ld      a,h
            or      l
            jr      nz,msh_loop$
            ret

mem_sys_text$:
            .db     'S','Y','S',' ','u','s','e','d','=',' ',0
mem_free_text$:
            .db     ' ','f','r','e','e','=',' ',0
mem_usr_text$:
            .db     'U','S','R',' ','u','s','e','d','=',' ',0

            .area   _INITIALIZED

mem_used$:
            .dw     0x0000
mem_free$:
            .dw     0x0000

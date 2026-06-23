            ;; boot.s
            ;;
            ;; early os bootstrap thread
            ;;
            ;; once init.s has brought the scheduler online, this OS thread
            ;; performs the first higher-level OS step:
            ;;
            ;;   - create one temporary completion event,
            ;;   - try the real ROM-selected boot device first,
            ;;   - mount the first usable FAT volume,
            ;;   - load `/SHELL.COM`,
            ;;   - hand the embedded XL image to process_load_com() so shell
            ;;     starts as a normal process with its own main thread,
            ;;   - keep that mounted boot volume around so later shell commands
            ;;     can resolve and launch `NAME.COM` files from the same disk.
            ;;
            ;; 2026-06-22   tstih
            .module boot

            .include "../partos.inc"
            .include "fat.inc"

            .equ    PROCESS_CMDLINE,     15
            .equ    THREAD_PROCESS,      22

            .globl  _kernel_bootstrap
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _thread_wait4events
            .globl  __thread_cleanup_terminated
            .globl  _thread_current
            .globl  _thread_exit
            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _fat_mount
            .globl  _fat_open
            .globl  _fat_read
            .globl  _process_load_com
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  _partos_get_boot_fs
            .globl  _partos_get_command_line
            .globl  _partos_run_command
            .globl  _boot_debug_stage
            .globl  _boot_debug_rc
            .globl  ctc_enable_tick
            .globl  __usr_heap
            .globl  boot_fs$
            .globl  boot_try_path$
            .globl  boot_try_device$
            .globl  __boot_after_evt_create
            .globl  __boot_try_sda
            .globl  __boot_try_fd0
            .globl  __boot_cleanup
            .globl  __boot_exit

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; boot_wait_one$(<hl> event)
            ;; ----------------------------------------------------------------
            ;; blocks the current bootstrap thread on one event. the scheduler
            ;; fast-path returns immediately if the event is already signaled.
            ;; ----------------------------------------------------------------
boot_wait_one$:
            ld      hl,#boot_event$
            ld      a,#1
            push    af
            inc     sp
            call    _ir_enable
            call    _thread_wait4events
            ret

            ;; ----------------------------------------------------------------
            ;; boot_status_at$(<hl> base, <bc> offset) -> <hl> status
            ;; ----------------------------------------------------------------
            ;; reads one 16-bit signed status field from a mounted fs/file
            ;; scratch object.
            ;; ----------------------------------------------------------------
boot_status_at$:
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ret

            ;; ----------------------------------------------------------------
            ;; boot_free_image$()
            ;; ----------------------------------------------------------------
            ;; frees the temporary shell image buffer if one is still owned by
            ;; the bootstrap thread. on a successful process_load_image() path
            ;; the owner is transferred to the new process first, so this helper
            ;; sees a cleared pointer and becomes a no-op.
            ;; ----------------------------------------------------------------
boot_free_image$:
            ld      de,(boot_image$)
            ld      a,d
            or      e
            ret     z
            push    de
            ld      hl,#__usr_heap
            call    _mem_free
            pop     bc
            xor     a
            ld      (boot_image$),a
            ld      (boot_image$ + 1),a
            ret

            ;; ----------------------------------------------------------------
            ;; boot_cleanup_loader$()
            ;; ----------------------------------------------------------------
            ;; destroys the temporary loader event (if any) and frees the
            ;; staging image buffer (if any). the mounted boot fs stays live.
            ;; ----------------------------------------------------------------
boot_cleanup_loader$:
            ld      hl,(boot_event$)
            ld      a,h
            or      l
            call    nz,_evt_destroy
            xor     a
            ld      (boot_event$),a
            ld      (boot_event$ + 1),a
            jp      boot_free_image$

            ;; ----------------------------------------------------------------
            ;; <a> <= boot_ascii_upper$(<a> ch)
            ;; ----------------------------------------------------------------
boot_ascii_upper$:
            cp      #'a'
            ret     c
            cp      #('z' + 1)
            ret     nc
            sub     #0x20
            ret

            ;; ----------------------------------------------------------------
            ;; boot_skip_spaces$(<hl> text) -> <hl> first_non_space
            ;; ----------------------------------------------------------------
boot_skip_spaces$:
bss_loop$:
            ld      a,(hl)
            cp      #' '
            ret     nz
            inc     hl
            jr      bss_loop$

            ;; ----------------------------------------------------------------
            ;; boot_lock_loader$() -> <a> 1 success / 0 busy
            ;; ----------------------------------------------------------------
            ;; serializes the shared boot-loader scratch area used by
            ;; _partos_run_command(). this keeps the launcher safe even when
            ;; multiple user threads try to start commands concurrently.
            ;; ----------------------------------------------------------------
boot_lock_loader$:
            call    _ir_disable
            ld      a,(boot_loader_busy$)
            or      a
            jr      nz,bll_busy$
            ld      a,#1
            ld      (boot_loader_busy$),a
            call    _ir_enable
            ld      a,#1
            ret
bll_busy$:
            call    _ir_enable
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; boot_unlock_loader$()
            ;; ----------------------------------------------------------------
boot_unlock_loader$:
            call    _ir_disable
            xor     a
            ld      (boot_loader_busy$),a
            jp      _ir_enable

            ;; ----------------------------------------------------------------
            ;; boot_prepare_command$(<hl> line) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; copies one user command line into boot_cmdline_buf$, trims
            ;; leading/trailing spaces, then extracts argv0 into boot_cmd_name$
            ;; so boot_build_command_path$ can resolve `/NAME.COM`.
            ;; ----------------------------------------------------------------
boot_prepare_command$:
            call    boot_skip_spaces$
            ld      de,#boot_cmdline_buf$
            ld      b,#0
bpc_copy$:
            ld      a,(hl)
            or      a
            jr      z,bpc_copied$
            ld      a,b
            cp      #63
            jr      nc,bpc_fail$
            ld      a,(hl)
            ld      (de),a
            inc     de
            inc     hl
            inc     b
            jr      bpc_copy$
bpc_copied$:
            xor     a
            ld      (de),a
            ld      a,b
            or      a
            jr      z,bpc_fail$
            ld      a,b
            ld      (boot_cmdline_len$),a

bpc_trim$:
            ld      a,(boot_cmdline_len$)
            or      a
            jr      z,bpc_fail$
            dec     a
            ld      c,a
            ld      b,#0
            ld      hl,#boot_cmdline_buf$
            add     hl,bc
            ld      a,(hl)
            cp      #' '
            jr      nz,bpc_tokenize$
            xor     a
            ld      (hl),a
            ld      a,c
            ld      (boot_cmdline_len$),a
            jr      bpc_trim$

bpc_tokenize$:
            ld      hl,#boot_cmdline_buf$
            ld      de,#boot_cmd_name$
            ld      b,#0
bpc_tok_loop$:
            ld      a,(hl)
            or      a
            jr      z,bpc_tok_done$
            cp      #' '
            jr      z,bpc_tok_done$
            ld      a,b
            cp      #12
            jr      nc,bpc_fail$
            ld      a,(hl)
            ld      (de),a
            inc     de
            inc     hl
            inc     b
            jr      bpc_tok_loop$
bpc_tok_done$:
            xor     a
            ld      (de),a
            ld      hl,#boot_cmd_name$
            jp      boot_build_command_path$

bpc_fail$:
            xor     a
            ld      (boot_cmdline_buf$),a
            ld      (boot_cmd_name$),a
            ld      (boot_cmdline_len$),a
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_attach_cmdline$(<de> process) -> <de> process
            ;; ----------------------------------------------------------------
            ;; makes a private heap-owned copy of the prepared command line and
            ;; stores it in process->cmdline. allocation failure is non-fatal:
            ;; the process still runs, it just sees an empty command line.
            ;; ----------------------------------------------------------------
boot_attach_cmdline$:
            ld      (boot_process$),de
            push    de                  ; owner = process
            ld      a,(boot_cmdline_len$)
            ld      e,a
            ld      d,#0
            inc     de                  ; include terminating NUL
            ld      hl,#__usr_heap
            call    _mem_allocate
            pop     bc
            ld      a,d
            or      e
            jr      nz,bac_have_buf$
            ld      de,(boot_process$)
            ret

bac_have_buf$:
            ld      (boot_cmd_copy$),de
            ex      de,hl               ; hl = allocated destination
            ld      de,#boot_cmdline_buf$
            ld      a,(boot_cmdline_len$)
            ld      c,a
            ld      b,#0
            inc     bc
            ex      de,hl               ; hl = source, de = destination
            ldir

            ld      de,(boot_process$)
            ex      de,hl               ; hl = process
            ld      bc,#PROCESS_CMDLINE
            add     hl,bc
            ld      de,(boot_cmd_copy$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ld      de,(boot_process$)
            ret

            ;; ----------------------------------------------------------------
            ;; boot_build_command_path$(<hl> name) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; normalizes one simple command name into:
            ;;   boot_cmd_path$  = "/NAME.COM"
            ;;   boot_pname_buf$ = "NAME"
            ;; accepted input:
            ;;   - 1..8 alpha/digit chars
            ;;   - optional ".com" suffix (case-insensitive)
            ;; process names are truncated to 7 chars + NUL because process_t
            ;; only carries MAX_PNAME_LEN = 8 including the terminator.
            ;; ----------------------------------------------------------------
boot_build_command_path$:
            ld      de,#boot_cmd_path$ + 1
            ld      ix,#boot_pname_buf$
            ld      b,#0                ; command length (max 8)
            ld      c,#0                ; pname bytes stored (max 7)

bbcp_loop$:
            ld      a,(hl)
            or      a
            jr      z,bbcp_done_base$
            cp      #'.'
            jr      z,bbcp_ext$
            call    boot_ascii_upper$
            cp      #'A'
            jr      c,bbcp_digit$
            cp      #('Z' + 1)
            jr      nc,bbcp_fail$
            jr      bbcp_store$
bbcp_digit$:
            cp      #'0'
            jr      c,bbcp_fail$
            cp      #('9' + 1)
            jr      nc,bbcp_fail$

bbcp_store$:
            ld      (boot_cmd_chr$),a
            ld      a,b
            cp      #8
            jr      nc,bbcp_fail$
            inc     b
            ld      a,(boot_cmd_chr$)
            ld      (de),a
            inc     de
            ld      a,c
            cp      #7
            jr      nc,bbcp_next$
            ld      a,(boot_cmd_chr$)
            ld      0(ix),a
            inc     ix
            inc     c
bbcp_next$:
            inc     hl
            jr      bbcp_loop$
bbcp_fail$:
            xor     a
            ld      (boot_cmd_path$),a
            ld      (boot_pname_buf$),a
            scf
            ret

bbcp_ext$:
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'C'
            jr      nz,bbcp_fail$
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'O'
            jr      nz,bbcp_fail$
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'M'
            jr      nz,bbcp_fail$
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,bbcp_fail$

bbcp_done_base$:
            ld      a,b
            or      a
            jr      z,bbcp_fail$
            ld      a,#'/'
            ld      (boot_cmd_path$),a
            ld      a,#'.'
            ld      (de),a
            inc     de
            ld      a,#'C'
            ld      (de),a
            inc     de
            ld      a,#'O'
            ld      (de),a
            inc     de
            ld      a,#'M'
            ld      (de),a
            inc     de
            xor     a
            ld      (de),a
            ld      0(ix),a
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; boot_try_path$(<hl> path, <de> pname) -> <de> process | 0
            ;; ----------------------------------------------------------------
            ;; opens one root-path candidate on the already mounted volume,
            ;; reads the whole COM image into a temporary heap buffer, then
            ;; starts it as one process. only 256-byte-aligned files fit the
            ;; current FAT block-I/O contract.
            ;; ----------------------------------------------------------------
boot_try_path$:
            ld      a,#0x30
            ld      (_boot_debug_stage),a
            ld      (boot_pname_ptr$),de
            ex      de,hl               ; de = path
            ld      bc,#boot_file$
            push    bc                  ; stacked file
            ld      bc,(boot_event$)
            push    bc                  ; stacked event
            ld      hl,#boot_fs$
            call    _fat_open
            ld      (_boot_debug_rc),de
            ld      a,d
            or      e
            jp      nz,btp_fail_zero$

btp_open_wait$:
            ld      hl,(boot_event$)
            call    boot_wait_one$
            ld      hl,#boot_file$
            ld      bc,#FATFILE_STATUS
            call    boot_status_at$
            ld      de,#FAT_EBUSY
            or      a
            sbc     hl,de
            jr      z,btp_open_wait$
            add     hl,de               ; undo sbc: hl = status again
            ld      a,h
            or      l
            jp      nz,btp_fail_zero$

            ;; validate a 16-bit, 256-byte-aligned image size:
            ;;   size[0] must be 0 (block aligned)
            ;;   size[2..3] must be 0 (fits the 16-bit loader contract)
            ld      hl,#boot_file$
            ld      bc,#FATFILE_SIZE
            add     hl,bc
            ld      e,(hl)              ; raw size low byte
            ld      a,e
            or      a
            jp      nz,btp_fail_zero$
            inc     hl
            ld      d,(hl)              ; raw size high byte
            inc     hl
            ld      a,(hl)
            or      a
            jp      nz,btp_fail_zero$
            inc     hl
            ld      a,(hl)
            or      a
            jp      nz,btp_fail_zero$
            ld      a,d
            or      a
            jp      z,btp_fail_zero$
            ld      (boot_img_size$),de

            ;; allocate one temporary image buffer from the user heap, owned by
            ;; the bootstrap thread for now; process_load_image() transfers that
            ;; ownership to the final process object.
            ld      hl,(_thread_current)
            push    hl                  ; owner = current bootstrap/caller thread
            ld      de,(boot_img_size$)
            ld      hl,#__usr_heap
            call    _mem_allocate
            pop     bc                  ; drop owner
            ld      a,d
            or      e
            jr      z,btp_fail_zero$
            ld      (boot_image$),de

            ;; read the whole aligned image into the heap buffer.
            ld      bc,(boot_event$)
            push    bc                  ; event
            ld      de,(boot_img_size$)
            push    de                  ; bytes
            ld      de,(boot_image$)
            ld      hl,#boot_file$
            call    _fat_read
            ld      (_boot_debug_rc),de
            ld      a,d
            or      e
            jr      nz,btp_fail_free$

            ld      hl,(boot_event$)
            call    boot_wait_one$
            ld      hl,#boot_file$
            ld      bc,#FATFILE_STATUS
            call    boot_status_at$
            ld      a,h
            or      l
            jr      nz,btp_fail_free$

            ;; the loaded file is a COM header wrapping one embedded XL image.
            ld      de,(boot_img_size$)
            push    de                  ; img_size
            ld      de,(boot_image$)
            ld      hl,(boot_pname_ptr$)
            call    _process_load_com
            ld      (_boot_debug_rc),de
            ld      a,d
            or      e
            jr      z,btp_fail_free$

            xor     a
            ld      (boot_image$),a
            ld      (boot_image$ + 1),a
            ret

btp_fail_free$:
            call    boot_free_image$
btp_fail_zero$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; boot_try_device$(<hl> dev_name) -> <de> process | 0
            ;; ----------------------------------------------------------------
            ;; mounts one device by name and tries the shell COM image from that
            ;; volume's root directory.
            ;; ----------------------------------------------------------------
boot_try_device$:
            ld      a,#0x20
            ld      (_boot_debug_stage),a
            ex      de,hl               ; de = device name
            ld      bc,(boot_event$)
            push    bc                  ; stacked event
            ld      hl,#boot_fs$
            call    _fat_mount
            ld      (_boot_debug_rc),de
            ld      a,d
            or      e
            jr      nz,btd_fail_zero$

            ld      hl,(boot_event$)
            call    boot_wait_one$
            ld      hl,#boot_fs$
            ld      bc,#FATFS_STATUS
            call    boot_status_at$
            ld      a,h
            or      l
            jr      nz,btd_fail_zero$

            ld      a,#1
            ld      (boot_fs_ready$),a
            ld      hl,#boot_shell_com$
            ld      de,#boot_shell_pname$
            jp      boot_try_path$

btd_fail_zero$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _partos_run_command(<hl> name)
            ;; ----------------------------------------------------------------
            ;; resolve `name` to `/NAME.COM` on the boot volume and launch it.
            ;; returns the new process object or 0 on failure.
            ;; ----------------------------------------------------------------
_partos_run_command::
            ld      a,(boot_fs_ready$)
            or      a
            jr      z,brc_fail_nolock$
            call    boot_lock_loader$
            or      a
            jr      z,brc_fail_nolock$
            call    boot_prepare_command$
            jr      c,brc_fail$
            ld      hl,#0x0000
            call    _evt_create
            ld      (boot_event$),de
            ld      a,d
            or      e
            jr      z,brc_fail$
            ld      hl,#boot_cmd_path$
            ld      de,#boot_pname_buf$
            call    boot_try_path$
            push    de
            ld      a,d
            or      e
            call    nz,boot_attach_cmdline$
            call    boot_cleanup_loader$
            call    boot_unlock_loader$
            pop     de
            ld      a,d
            or      e
            ret     z
            ;; Give the freshly launched process one immediate cooperative time
            ;; slice before the shell falls back into keyboard polling. The
            ;; child then reaches its own waits/exits even if the periodic tick
            ;; source is momentarily absent or late.
            rst     0x18
            call    _ir_disable
            call    __thread_cleanup_terminated
            call    _ir_enable
            ret

brc_fail_nolock$:
            ld      de,#0x0000
            ret
brc_fail$:
            call    boot_cleanup_loader$
            call    boot_unlock_loader$
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _partos_get_boot_fs()
            ;; ----------------------------------------------------------------
            ;; returns the mounted boot filesystem kept alive by the bootstrap,
            ;; or 0 when no boot volume is available.
            ;; ----------------------------------------------------------------
_partos_get_boot_fs::
            ld      a,(boot_fs_ready$)
            or      a
            jr      z,pgbf_fail$
            ld      de,#boot_fs$
            ret
pgbf_fail$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _partos_get_command_line()
            ;; ----------------------------------------------------------------
            ;; returns the current process command line, or a shared empty
            ;; string when the caller has no owning process or args.
            ;; ----------------------------------------------------------------
_partos_get_command_line::
            call    _ir_disable
            ld      hl,(_thread_current)
            ld      a,h
            or      l
            jr      z,pgcl_empty$
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ld      a,h
            or      l
            jr      z,pgcl_empty$
            ld      bc,#PROCESS_CMDLINE
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            jr      nz,pgcl_done$
pgcl_empty$:
            ld      de,#boot_empty_cmd$
pgcl_done$:
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; _kernel_bootstrap()
            ;; ----------------------------------------------------------------
            ;; scheduler-started kernel thread that registers a temporary wait
            ;; event, mirrors the ROM boot-device order and starts the first
            ;; shell process if one loadable image is found.
            ;; ----------------------------------------------------------------
_kernel_bootstrap::
            ld      a,#0x10
            ld      (_boot_debug_stage),a
            xor     a
            ld      (_boot_debug_rc),a
            ld      (_boot_debug_rc + 1),a
            ld      hl,#0x0000
            call    _evt_create
            ld      (boot_event$),de
            call    _ir_enable
__boot_after_evt_create::
            ld      a,d
            or      e
            jr      z,kb_exit$
            ld      a,#0x11
            ld      (_boot_debug_stage),a

            xor     a
            ld      (boot_fs_ready$),a
__boot_try_sda::
            ld      a,#0x12
            ld      (_boot_debug_stage),a
            ld      hl,#boot_dev_sda$
            call    boot_try_device$
            ld      a,d
            or      e
            jr      nz,kb_cleanup$
__boot_try_fd0::
            ld      hl,#boot_dev_fd0$
            call    boot_try_device$

__boot_cleanup::
kb_cleanup$:
            ld      a,d
            or      e
            call    nz,ctc_enable_tick ; shell is live: start the periodic tick
            ld      a,#0x14
            ld      (_boot_debug_stage),a
            call    boot_cleanup_loader$
            xor     a
            ld      (boot_loader_busy$),a

__boot_exit::
kb_exit$:
            ld      a,#0x15
            ld      (_boot_debug_stage),a
            ld      hl,(_thread_current)
            ld      de,#16
            add     hl,de
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            ld      hl,(_thread_current)
            jp      _thread_exit

            .area   _INITIALIZED

boot_dev_fd0$:
            .db     'f','d','0',0
boot_dev_sda$:
            .db     's','d','a',0
boot_shell_com$:
            .db     '/','S','H','E','L','L','.','C','O','M',0
boot_shell_pname$:
            .db     's','h','e','l','l',0
boot_loader_busy$:
            .db     0

            .area   _SYSVARS

boot_event$:
            .ds     2
boot_image$:
            .ds     2
boot_img_size$:
            .ds     2
boot_pname_ptr$:
            .ds     2
boot_fs_ready$:
            .ds     1
boot_cmd_chr$:
            .ds     1
boot_cmd_path$:
            .ds     16
boot_cmd_name$:
            .ds     13
boot_cmdline_len$:
            .ds     1
boot_process$:
            .ds     2
boot_cmd_copy$:
            .ds     2
_boot_debug_rc::
            .ds     2
_boot_debug_stage::
            .ds     1
boot_pname_buf$:
            .ds     8
boot_cmdline_buf$:
            .ds     64
boot_fs$:
            .ds     FATFS_SIZE
boot_file$:
            .ds     FATFILE_SIZEOF
boot_empty_cmd$:
            .db     0

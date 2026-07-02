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
            .globl  _partos_get_current_dir
            .globl  _partos_run_command
            .globl  _boot_debug_stage
            .globl  _boot_debug_rc
            .globl  ctc_enable_tick
            .globl  __usr_heap
            .globl  boot_event$
            .globl  boot_fs_ready$
            .globl  boot_loader_busy$
            .globl  boot_fs$
            .globl  boot_file$
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
            call    boot_destroy_event$
            jp      boot_free_image$

            ;; ----------------------------------------------------------------
            ;; boot_destroy_event$()
            ;; ----------------------------------------------------------------
            ;; drops the shared loader event handle if one is live.
            ;; ----------------------------------------------------------------
boot_destroy_event$:
            ld      hl,(boot_event$)
            ld      a,h
            or      l
            call    nz,_evt_destroy
            xor     a
            ld      (boot_event$),a
            ld      (boot_event$ + 1),a
            ret

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
            ;; <a> <= boot_ascii_lower$(<a> ch)
            ;; ----------------------------------------------------------------
boot_ascii_lower$:
            cp      #'A'
            ret     c
            cp      #('Z' + 1)
            ret     nc
            add     a,#0x20
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
            ;; so boot_build_command_path$ can resolve `name.com` against the
            ;; current working directory.
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
            cp      #63
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
            ;; boot_build_command_path$(<hl> name) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; resolves one command token into:
            ;;   boot_cmd_path$  = "/path/to/name[.com]"
            ;;   boot_cmd_name$  = "name"
            ;;   boot_pname_buf$ = "name"
            ;; accepted input:
            ;;   - relative or absolute paths
            ;;   - "." and ".." path segments for navigation
            ;;   - final segment 1..8 alpha/digit chars
            ;;   - optional ".com" suffix (case-insensitive)
            ;; process names are truncated to 7 chars + NUL because process_t
            ;; only carries MAX_PNAME_LEN = 8 including the terminator.
            ;; ----------------------------------------------------------------
boot_build_command_path$:
            push    hl
            call    boot_resolve_command_path$
            pop     hl
            jr      c,bbcp_fail$
            call    boot_build_command_leaf$
            jr      c,bbcp_fail$
            ld      a,(boot_cmd_has_ext$)
            or      a
            ret     nz
            call    boot_append_command_ext$
            jr      c,bbcp_fail$
            ret
bbcp_fail$:
            xor     a
            ld      (boot_cmd_path$),a
            ld      (boot_cmd_name$),a
            ld      (boot_pname_buf$),a
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_resolve_command_path$(<hl> token) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; resolves one relative or absolute command path into the mounted
            ;; boot-volume namespace, normalizing duplicate slashes plus "." and
            ;; ".." segments on the way. the final ".com" suffix is handled by
            ;; boot_append_command_ext$ after the last path segment is checked.
            ;; ----------------------------------------------------------------
boot_resolve_command_path$:
            push    hl
            ld      de,#boot_cmd_path$
            ld      a,(hl)
            cp      #'/'
            jr      z,brcp_root$

            ld      hl,#boot_cwd$
            ld      b,#0

brcp_copy_cwd$:
            ld      a,(hl)
            or      a
            jr      z,brcp_cwd_done$
            ld      c,a
            ld      a,b
            cp      #63
            jr      nc,brcp_fail_pop$
            ld      a,c
            ld      (de),a
            inc     de
            inc     hl
            inc     b
            jr      brcp_copy_cwd$

brcp_cwd_done$:
            ld      a,b
            or      a
            jr      nz,brcp_base_ready$

brcp_root$:
            ld      a,#'/'
            ld      (boot_cmd_path$),a
            ld      b,#1
            ld      de,#boot_cmd_path$ + 1

brcp_base_ready$:
            xor     a
            ld      (de),a
            ld      a,b
            ld      (boot_path_len$),a
            pop     hl

brcp_skip_slashes$:
            ld      a,(hl)
            cp      #'/'
            jr      nz,brcp_segment$
            inc     hl
            jr      brcp_skip_slashes$

brcp_segment$:
            ld      a,(hl)
            or      a
            jr      z,brcp_done$
            ld      (boot_seg_ptr$),hl
            ld      b,#0

brcp_measure$:
            ld      a,(hl)
            or      a
            jr      z,brcp_have_segment$
            cp      #'/'
            jr      z,brcp_have_segment$
            inc     hl
            inc     b
            jr      brcp_measure$

brcp_have_segment$:
            ld      a,b
            or      a
            jr      z,brcp_skip_slashes$
            push    hl
            ld      hl,(boot_seg_ptr$)
            ld      a,b
            cp      #1
            jr      nz,brcp_check_up$
            ld      a,(hl)
            cp      #'.'
            jr      z,brcp_segment_done$

brcp_check_up$:
            ld      a,b
            cp      #2
            jr      nz,brcp_append$
            ld      a,(hl)
            cp      #'.'
            jr      nz,brcp_append$
            inc     hl
            ld      a,(hl)
            cp      #'.'
            jr      nz,brcp_append$
            call    boot_pop_path_segment$
            jr      brcp_segment_done$

brcp_append$:
            call    boot_append_resolved_segment$

brcp_segment_done$:
            pop     hl
            jr      c,brcp_fail$
            jr      brcp_skip_slashes$

brcp_done$:
            or      a
            ret

brcp_fail_pop$:
            pop     hl
brcp_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_append_resolved_segment$(<hl> src, <b> len) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; appends one non-special path segment to boot_cmd_path$ and keeps
            ;; boot_path_len$ in sync with the normalized absolute path.
            ;; ----------------------------------------------------------------
boot_append_resolved_segment$:
            ld      a,b
            ld      (boot_cmd_chr$),a
            push    hl
            ld      a,(boot_path_len$)
            ld      c,a
            ld      b,#0
            ld      hl,#boot_cmd_path$
            add     hl,bc
            ld      d,h
            ld      e,l
            ld      a,c
            cp      #1
            jr      z,bars_copy$
            dec     hl
            ld      a,(hl)
            inc     hl
            cp      #'/'
            jr      z,bars_copy$
            ld      a,c
            cp      #63
            jr      nc,bars_fail_pop$
            ld      a,#'/'
            ld      (de),a
            inc     de
            inc     c

bars_copy$:
            pop     hl
            ld      a,(boot_cmd_chr$)
            add     a,c
            cp      #64
            jr      nc,bars_fail$
            ld      c,a
            ld      a,(boot_cmd_chr$)
            ld      b,a

bars_copy_loop$:
            ld      a,b
            or      a
            jr      z,bars_done$
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            djnz    bars_copy_loop$

bars_done$:
            xor     a
            ld      (de),a
            ld      a,c
            ld      (boot_path_len$),a
            or      a
            ret

bars_fail_pop$:
            pop     hl
bars_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_pop_path_segment$()
            ;; ----------------------------------------------------------------
            ;; removes the last segment from the normalized boot_cmd_path$
            ;; buffer, but never climbs above the boot-volume root.
            ;; ----------------------------------------------------------------
boot_pop_path_segment$:
            ld      a,(boot_path_len$)

bpps_scan$:
            cp      #1
            jr      z,bpps_trim_done$
            ld      c,a
            ld      b,#0
            ld      hl,#boot_cmd_path$
            add     hl,bc
            dec     hl
            ld      a,(hl)
            cp      #'/'
            jr      z,bpps_found_slash$
            ld      a,c
            dec     a
            jr      bpps_scan$

bpps_found_slash$:
            ld      a,c

bpps_trim_done$:
            cp      #1
            jr      z,bpps_store$
            dec     a

bpps_store$:
            ld      c,a
            ld      b,#0
            ld      hl,#boot_cmd_path$
            add     hl,bc
            xor     a
            ld      (hl),a
            ld      a,c
            ld      (boot_path_len$),a
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; boot_build_command_leaf$(<hl> token) -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; locates the final syntactic path segment in the original command
            ;; token, validates it as one executable leaf name, then fills:
            ;;   boot_cmd_name$    = "name"
            ;;   boot_pname_buf$   = "name"
            ;;   boot_cmd_has_ext$ = 0/1 for an explicit ".com" suffix
            ;; ----------------------------------------------------------------
boot_build_command_leaf$:
bbcl_seek$:
            ld      a,(hl)
            or      a
            jp      z,bbcl_fail$
            cp      #'/'
            jr      nz,bbcl_segment$
            inc     hl
            jr      bbcl_seek$

bbcl_segment$:
            push    hl

bbcl_scan$:
            ld      a,(hl)
            or      a
            jr      z,bbcl_final$
            cp      #'/'
            jr      z,bbcl_after_segment$
            inc     hl
            jr      bbcl_scan$

bbcl_after_segment$:
            pop     de

bbcl_skip$:
            ld      a,(hl)
            cp      #'/'
            jr      nz,bbcl_skip_done$
            inc     hl
            jr      bbcl_skip$

bbcl_skip_done$:
            ld      a,(hl)
            or      a
            jp      z,bbcl_fail$
            jr      bbcl_segment$

bbcl_final$:
            pop     hl
            ld      de,#boot_cmd_name$
            ld      ix,#boot_pname_buf$
            ld      b,#0
            ld      c,#0
            xor     a
            ld      (boot_cmd_has_ext$),a

bbcl_name_loop$:
            ld      a,(hl)
            or      a
            jr      z,bbcl_name_done$
            cp      #'.'
            jr      z,bbcl_ext$
            call    boot_ascii_upper$
            cp      #'A'
            jr      c,bbcl_digit$
            cp      #('Z' + 1)
            jr      nc,bbcl_fail$
            jr      bbcl_store$

bbcl_digit$:
            cp      #'0'
            jr      c,bbcl_fail$
            cp      #('9' + 1)
            jr      nc,bbcl_fail$

bbcl_store$:
            call    boot_ascii_lower$
            ld      (boot_cmd_chr$),a
            ld      a,b
            cp      #8
            jr      nc,bbcl_fail$
            inc     b
            ld      a,(boot_cmd_chr$)
            ld      (de),a
            inc     de
            ld      a,c
            cp      #7
            jr      nc,bbcl_next$
            ld      a,(boot_cmd_chr$)
            ld      0(ix),a
            inc     ix
            inc     c

bbcl_next$:
            inc     hl
            jr      bbcl_name_loop$

bbcl_ext$:
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'C'
            jr      nz,bbcl_fail$
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'O'
            jr      nz,bbcl_fail$
            inc     hl
            ld      a,(hl)
            call    boot_ascii_upper$
            cp      #'M'
            jr      nz,bbcl_fail$
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,bbcl_fail$
            ld      a,#1
            ld      (boot_cmd_has_ext$),a

bbcl_name_done$:
            ld      a,b
            or      a
            jr      z,bbcl_fail$
            xor     a
            ld      (de),a
            ld      0(ix),a
            or      a
            ret

bbcl_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_append_command_ext$() -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; appends ".com" to the normalized boot_cmd_path$ when the caller
            ;; omitted the executable suffix on the final token segment.
            ;; ----------------------------------------------------------------
boot_append_command_ext$:
            ld      a,(boot_path_len$)
            cp      #60
            jr      nc,bace_fail$
            ld      c,a
            ld      b,#0
            ld      hl,#boot_cmd_path$
            add     hl,bc
            ld      a,#'.'
            ld      (hl),a
            inc     hl
            ld      a,#'c'
            ld      (hl),a
            inc     hl
            ld      a,#'o'
            ld      (hl),a
            inc     hl
            ld      a,#'m'
            ld      (hl),a
            inc     hl
            xor     a
            ld      (hl),a
            ld      a,c
            add     a,#4
            ld      (boot_path_len$),a
            or      a
            ret

bace_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; boot_build_root_command_path$() -> cf=1 invalid
            ;; ----------------------------------------------------------------
            ;; composes one rooted fallback path in boot_cmd_path$ from the
            ;; normalized command leaf stored in boot_cmd_name$.
            ;; ----------------------------------------------------------------
boot_build_root_command_path$:
            ld      a,#'/'
            ld      (boot_cmd_path$),a
            xor     a
            ld      (boot_cmd_path$ + 1),a
            ld      a,#1
            ld      (boot_path_len$),a
            ld      hl,#boot_cmd_name$
            ld      b,#0

bbrcp_len$:
            ld      a,(hl)
            or      a
            jr      z,bbrcp_append$
            inc     hl
            inc     b
            jr      bbrcp_len$

bbrcp_append$:
            ld      hl,#boot_cmd_name$
            call    boot_append_resolved_segment$
            ret     c
            jp      boot_append_command_ext$

            ;; ----------------------------------------------------------------
            ;; boot_cstr_eq$(<hl> lhs, <de> rhs) -> Z equal / NZ different
            ;; ----------------------------------------------------------------
boot_cstr_eq$:
bce_loop$:
            ld      a,(de)
            cp      (hl)
            ret     nz
            or      a
            ret     z
            inc     de
            inc     hl
            jr      bce_loop$

            ;; ----------------------------------------------------------------
            ;; <a> <= boot_command_is_global$()
            ;; ----------------------------------------------------------------
            ;; shell-facing root fallback stays enabled for a small command set
            ;; so filesystem tools remain available after `cd`, while ordinary
            ;; app names resolve strictly from cwd unless a path is spelled out.
            ;; ----------------------------------------------------------------
boot_command_is_global$:
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_ls$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_cd$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_ps$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_cat$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_mkdir$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_rmdir$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_del$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_cp$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_mv$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_clear$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_echo$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            ld      hl,#boot_cmd_name$
            ld      de,#boot_global_help$
            call    boot_cstr_eq$
            jr      z,bcg_yes$
            xor     a
            ret

bcg_yes$:
            ld      a,#1
            ret

            ;; ----------------------------------------------------------------
            ;; boot_try_path$(<hl> path, <de> pname) -> <de> process | 0
            ;; ----------------------------------------------------------------
            ;; opens one absolute-path candidate on the already mounted volume,
            ;; reads the whole COM image into a temporary heap buffer, then
            ;; starts it as one process. only 256-byte-aligned files fit the
            ;; current FAT block-I/O contract.
            ;; ----------------------------------------------------------------
boot_try_path$:
            ld      a,#0x30
            ld      (_boot_debug_stage),a
            push    hl
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            pop     hl
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
            jr      z,btp_open_wait$
            ld      (boot_try_status$),de
            jp      btp_fail_zero$

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
            jr      z,btp_size_check$
            ld      (boot_try_status$),hl
            jp      btp_fail_zero$

            ;; validate a 16-bit, 256-byte-aligned image size:
            ;;   size[0] must be 0 (block aligned)
            ;;   size[2..3] must be 0 (fits the 16-bit loader contract)
btp_size_check$:
            ld      hl,#boot_file$
            ld      bc,#FATFILE_SIZE
            add     hl,bc
            ld      e,(hl)              ; raw size low byte
            ld      a,e
            or      a
            jr      z,btp_size_hi$
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            jp      btp_fail_zero$
btp_size_hi$:
            inc     hl
            ld      d,(hl)              ; raw size high byte
            inc     hl
            ld      a,(hl)
            or      a
            jr      z,btp_size_hi2$
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            jp      btp_fail_zero$
btp_size_hi2$:
            inc     hl
            ld      a,(hl)
            or      a
            jr      z,btp_size_nonzero$
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            jp      btp_fail_zero$
btp_size_nonzero$:
            ld      a,d
            or      a
            jr      nz,btp_have_size$
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            jp      btp_fail_zero$
btp_have_size$:
            ld      (boot_img_size$),de

            ;; allocate one temporary image buffer from the user heap, owned by
            ;; the bootstrap thread for now; process_load_image() transfers that
            ;; ownership to the final process object. keep one extra 256-byte
            ;; guard sector after the COM payload so the following free-block
            ;; header survives any whole-sector overrun in the current read path.
            ld      hl,(_thread_current)
            push    hl                  ; owner = current bootstrap/caller thread
            ld      de,(boot_img_size$)
            inc     d                   ; +0x0100 guard (sizes are 256-byte aligned)
            ld      hl,#__usr_heap
            call    _mem_allocate
            pop     bc                  ; drop owner
            ld      a,d
            or      e
            jr      nz,btp_have_image$
            ld      hl,#FAT_ENOMEM
            ld      (boot_try_status$),hl
            jr      btp_fail_zero$
btp_have_image$:
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
            jr      z,btp_read_wait$
            ld      (boot_try_status$),de
            jr      btp_fail_free$

btp_read_wait$:
            ld      hl,(boot_event$)
            call    boot_wait_one$
            ld      hl,#boot_file$
            ld      bc,#FATFILE_STATUS
            call    boot_status_at$
            ld      a,h
            or      l
            jr      z,btp_process_load$
            ld      (boot_try_status$),hl
            jr      btp_fail_free$

            ;; FAT no longer needs the loader event once the COM image is fully
            ;; in RAM. Free it before process/thread startup so the child and
            ;; its own app event can use the shared heap slots instead.
btp_process_load$:
            call    boot_destroy_event$

            ;; the loaded file is a COM header wrapping one embedded XL image.
            ld      de,(boot_img_size$)
            push    de                  ; img_size
            ld      de,(boot_image$)
            ld      hl,(boot_pname_ptr$)
            call    _process_load_com
            ld      (_boot_debug_rc),de
            ld      a,d
            or      e
            jr      nz,btp_success$
            ld      hl,#FAT_EINVAL
            ld      (boot_try_status$),hl
            jr      btp_fail_free$

btp_success$:
            ld      hl,#FAT_OK
            ld      (boot_try_status$),hl
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
            ld      a,#'/'
            ld      (boot_cwd$),a
            xor     a
            ld      (boot_cwd$ + 1),a
            ld      hl,#boot_shell_com$
            ld      de,#boot_shell_pname$
            jp      boot_try_path$

btd_fail_zero$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _partos_run_command(<hl> name)
            ;; ----------------------------------------------------------------
            ;; resolve `name` to `<cwd>/name.com` on the boot volume and launch
            ;; it. returns the new process object or 0 on failure.
            ;; ----------------------------------------------------------------
_partos_run_command::
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
            ld      a,d
            or      e
            jr      nz,brc_done$
            ;; boot_try_path$ failed. every branch below is a launch failure,
            ;; so it must fall through with de=0 -- otherwise the ENOENT compare
            ;; (which clobbers de) or the is_global/build_root helpers leave
            ;; garbage in de and the shell mistakes it for a live process,
            ;; swallowing the "command not found" error instead of printing it.
            ld      hl,(boot_try_status$)
            ld      de,#FAT_ENOENT
            or      a
            sbc     hl,de
            jr      nz,brc_zero$
            call    boot_command_is_global$
            or      a
            jr      z,brc_zero$
            call    boot_build_root_command_path$
            jr      c,brc_zero$
            ld      hl,#boot_cmd_path$
            ld      de,#boot_pname_buf$
            call    boot_try_path$
            jr      brc_done$

brc_zero$:
            ld      de,#0x0000
brc_done$:
            push    de
            call    boot_cleanup_loader$
            call    boot_unlock_loader$
            pop     de
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
            ;; returns the active shell filesystem. userland may update this
            ;; shared fs object after it mounts another volume successfully.
            ;; ----------------------------------------------------------------
_partos_get_boot_fs::
            ld      de,#boot_fs$
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
            ;; <de> <= _partos_get_current_dir()
            ;; ----------------------------------------------------------------
_partos_get_current_dir::
            ld      de,#boot_cwd$
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
            .db     '/','s','h','e','l','l','.','c','o','m',0
boot_shell_pname$:
            .db     's','h','e','l','l',0
boot_global_ls$:
            .db     'l','s',0
boot_global_cd$:
            .db     'c','d',0
boot_global_ps$:
            .db     'p','s',0
boot_global_cat$:
            .db     'c','a','t',0
boot_global_mkdir$:
            .db     'm','k','d','i','r',0
boot_global_rmdir$:
            .db     'r','m','d','i','r',0
boot_global_del$:
            .db     'd','e','l',0
boot_global_cp$:
            .db     'c','p',0
boot_global_mv$:
            .db     'm','v',0
boot_global_clear$:
            .db     'c','l','e','a','r',0
boot_global_echo$:
            .db     'e','c','h','o',0
boot_global_help$:
            .db     'h','e','l','p',0
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
boot_cmd_has_ext$:
            .ds     1
boot_path_len$:
            .ds     1
boot_cmd_path$:
            .ds     64
boot_cmd_name$:
            .ds     64
boot_cmdline_len$:
            .ds     1
boot_try_status$:
            .ds     2
boot_seg_ptr$:
            .ds     2
_boot_debug_rc::
            .ds     2
_boot_debug_stage::
            .ds     1
boot_pname_buf$:
            .ds     8
boot_cmdline_buf$:
            .ds     64
boot_cwd$:
            .ds     64
boot_fs$:
            .ds     FATFS_SIZE
boot_file$:
            .ds     FATFILE_SIZEOF
boot_empty_cmd$:
            .db     0

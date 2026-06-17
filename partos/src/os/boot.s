            ;; boot.s
            ;;
            ;; early os bootstrap thread
            ;;
            ;; once init.s has brought the scheduler online, this OS thread
            ;; performs the first higher-level OS step:
            ;;
            ;;   - create one temporary completion event,
            ;;   - try the ROM boot-device order (`fd0`, then `sda`),
            ;;   - mount the first usable FAT volume,
            ;;   - load `/SHELL.XL`,
            ;;   - hand the XL image to process_load_image() so shell starts as
            ;;     a normal process with its own main thread.
            ;;
            ;; current limitation:
            ;;   the ROM does not yet pass the exact boot-device id into the
            ;;   kernel handoff, so this loader mirrors ROM policy and tries the
            ;;   same device order instead of knowing the precise source.
            ;;
            ;; 2026-06-17   tstih
            .module boot

            .include "../partos.inc"
            .include "fat.inc"

            .globl  _kernel_bootstrap
            .globl  _thread_wait4events
            .globl  _thread_current
            .globl  _thread_exit
            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _fat_mount
            .globl  _fat_open
            .globl  _fat_read
            .globl  _process_load_image
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  __usr_heap

            .equ    APP_STACK_SIZE,     512

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; boot_wait_one$(<hl> event)
            ;; ----------------------------------------------------------------
            ;; blocks the current bootstrap thread on one event. the scheduler
            ;; fast-path returns immediately if the event is already signaled.
            ;; ----------------------------------------------------------------
boot_wait_one$:
            push    hl
            ld      hl,#0
            add     hl,sp
            ld      a,#1
            push    af
            inc     sp
            call    _thread_wait4events
            pop     de
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
            ;; boot_try_path$(<hl> path) -> <de> process | 0
            ;; ----------------------------------------------------------------
            ;; opens one root-path candidate on the already mounted volume,
            ;; reads the whole XL image into a temporary heap buffer, then
            ;; starts it as one process. only 256-byte-aligned images fit the
            ;; current FAT block-I/O contract.
            ;; ----------------------------------------------------------------
boot_try_path$:
            ex      de,hl               ; de = path
            ld      bc,(boot_event$)
            push    bc                  ; stacked event
            ld      bc,#boot_file$
            push    bc                  ; stacked file
            ld      hl,#boot_fs$
            call    _fat_open
            ld      a,d
            or      e
            jp      nz,btp_fail_zero$

            ld      hl,(boot_event$)
            call    boot_wait_one$
            ld      hl,#boot_file$
            ld      bc,#FATFILE_STATUS
            call    boot_status_at$
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
            jr      nz,btp_fail_zero$
            inc     hl
            ld      d,(hl)              ; raw size high byte
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,btp_fail_zero$
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,btp_fail_zero$
            ld      a,d
            or      a
            jr      z,btp_fail_zero$
            ld      (boot_img_size$),de

            ;; allocate one temporary image buffer from the user heap, owned by
            ;; the bootstrap thread for now; process_load_image() transfers that
            ;; ownership to the final process object.
            ld      hl,(_thread_current)
            ld      b,h
            ld      c,l
            ld      de,(boot_img_size$)
            ld      hl,#__usr_heap
            call    _mem_allocate
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

            ;; shell image is assumed to be one XL relocatable executable.
            ld      de,#APP_STACK_SIZE
            push    de                  ; stack_size
            ld      de,(boot_img_size$)
            push    de                  ; img_size
            ld      de,(boot_image$)
            ld      hl,#boot_pname$
            call    _process_load_image
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
            ;; mounts one device by name and tries the shell XL image from that
            ;; volume's root directory.
            ;; ----------------------------------------------------------------
boot_try_device$:
            ex      de,hl               ; de = device name
            ld      bc,(boot_event$)
            push    bc                  ; stacked event
            ld      hl,#boot_fs$
            call    _fat_mount
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

            ld      hl,#boot_shell_xl$
            jp      boot_try_path$

btd_fail_zero$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; _kernel_bootstrap()
            ;; ----------------------------------------------------------------
            ;; scheduler-started kernel thread that registers a temporary wait
            ;; event, mirrors the ROM boot-device order and starts the first
            ;; shell process if one loadable image is found.
            ;; ----------------------------------------------------------------
_kernel_bootstrap::
            ld      hl,#0x0000
            call    _evt_create
            ld      (boot_event$),de
            ld      a,d
            or      e
            jr      z,kb_exit$

            ld      hl,#boot_dev_fd0$
            call    boot_try_device$
            ld      a,d
            or      e
            jr      nz,kb_cleanup$

            ld      hl,#boot_dev_sda$
            call    boot_try_device$

kb_cleanup$:
            ld      hl,(boot_event$)
            ld      a,h
            or      l
            call    nz,_evt_destroy
            xor     a
            ld      (boot_event$),a
            ld      (boot_event$ + 1),a
            call    boot_free_image$

kb_exit$:
            ld      hl,(_thread_current)
            jp      _thread_exit

            .area   _INITIALIZED

boot_dev_fd0$:
            .db     'f','d','0',0
boot_dev_sda$:
            .db     's','d','a',0
boot_shell_xl$:
            .db     '/','S','H','E','L','L','.','X','L',0
boot_pname$:
            .db     's','h','e','l','l',0

            .area   _SYSVARS

boot_event$:
            .ds     2
boot_image$:
            .ds     2
boot_img_size$:
            .ds     2
boot_fs$:
            .ds     FATFS_SIZE
boot_file$:
            .ds     FATFILE_SIZEOF

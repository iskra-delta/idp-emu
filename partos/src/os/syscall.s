            ;; syscall.s
            ;;
            ;; public PartOS syscall table
            ;;
            ;; this module registers one named service ("partos") whose
            ;; function-table pointer is not a flat anonymous array anymore,
            ;; but one stable struct-shaped table with grouped, descriptive
            ;; entry names declared in include/partos.h.
            ;;
            ;; the first entry refreshes and returns a compact shared
            ;; sys_info snapshot that exposes the live kernel lists, heaps,
            ;; model bytes and cached NVRAM block.
            ;;
            ;; 2026-06-19   tstih
            .module syscall

            .globl  _syscall_init
            .globl  _partos_get_sys_info
            .globl  _syscall_service
            .globl  _syscall_table
            .globl  _partos_clear_screen
            .globl  _partos_set_xy
            .globl  _partos_write_console
            .globl  _partos_peek_keyboard
            .globl  _partos_read_keyboard
            .globl  _partos_get_boot_fs
            .globl  _partos_get_command_line
            .globl  _partos_run_command

            .globl  _svc_register
            .globl  _svc_unregister
            .globl  _svc_query

            .globl  _list_find
            .globl  _list_iterate
            .globl  _list_insert
            .globl  _list_remove
            .globl  _list_append
            .globl  _mem_init
            .globl  _mem_allocate
            .globl  _mem_free

            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set
            .globl  _vector_set

            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _evt_set
            .globl  _tmr_install
            .globl  _tmr_uninstall

            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_suspend
            .globl  _thread_exit
            .globl  _thread_wait4events

            .globl  _process_start
            .globl  _process_load_image
            .globl  _process_load_com
            .globl  _process_wait
            .globl  _process_exit

            .globl  _fat_mount_dev
            .globl  _fat_mount
            .globl  _fat_lookup
            .globl  _fat_open
            .globl  _fat_create
            .globl  _fat_read
            .globl  _fat_write
            .globl  _fat_readdir
            .globl  _fat_unlink
            .globl  _fat_mkdir
            .globl  _fat_rmdir

            .globl  __sys_version
            .globl  __sys_model
            .globl  __sys_flags
            .globl  __sys_meta1
            .globl  __sys_info
            .globl  __sys_info_version
            .globl  __sys_info_model
            .globl  __sys_info_flags
            .globl  __sys_info_meta1
            .globl  __sys_info_drv_first
            .globl  __sys_info_dev_first
            .globl  __sys_info_svc_first
            .globl  __sys_info_process_first
            .globl  __sys_info_evt_first
            .globl  __sys_info_tmr_first
            .globl  __sys_info_thread_current
            .globl  __sys_info_thread_running
            .globl  __sys_info_thread_waiting
            .globl  __sys_info_thread_suspended
            .globl  __sys_info_thread_terminated
            .globl  __sys_info_sys_heap
            .globl  __sys_info_usr_heap

            .globl  _drv_first
            .globl  _dev_first
            .globl  __svc_first
            .globl  _process_first
            .globl  __evt_first
            .globl  __tmr_first
            .globl  _thread_current
            .globl  _thread_first_running
            .globl  _thread_first_waiting
            .globl  _thread_first_suspended
            .globl  _thread_first_terminated
            .globl  __sys_heap
            .globl  __usr_heap

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _partos_get_sys_info()
            ;; ----------------------------------------------------------------
            ;; refreshes the shared sys_info snapshot from the live kernel
            ;; globals, then returns a pointer to that structure.
            ;; ----------------------------------------------------------------
_partos_get_sys_info::
            call    _ir_disable

            ld      a,(__sys_version)
            ld      (__sys_info_version),a
            ld      a,(__sys_model)
            ld      (__sys_info_model),a
            ld      a,(__sys_flags)
            ld      (__sys_info_flags),a
            ld      a,(__sys_meta1)
            ld      (__sys_info_meta1),a

            ld      hl,(_drv_first)
            ld      (__sys_info_drv_first),hl
            ld      hl,(_dev_first)
            ld      (__sys_info_dev_first),hl
            ld      hl,(__svc_first)
            ld      (__sys_info_svc_first),hl
            ld      hl,(_process_first)
            ld      (__sys_info_process_first),hl
            ld      hl,(__evt_first)
            ld      (__sys_info_evt_first),hl
            ld      hl,(__tmr_first)
            ld      (__sys_info_tmr_first),hl
            ld      hl,(_thread_current)
            ld      (__sys_info_thread_current),hl
            ld      hl,(_thread_first_running)
            ld      (__sys_info_thread_running),hl
            ld      hl,(_thread_first_waiting)
            ld      (__sys_info_thread_waiting),hl
            ld      hl,(_thread_first_suspended)
            ld      (__sys_info_thread_suspended),hl
            ld      hl,(_thread_first_terminated)
            ld      (__sys_info_thread_terminated),hl

            ld      hl,#__sys_heap
            ld      (__sys_info_sys_heap),hl
            ld      hl,#__usr_heap
            ld      (__sys_info_usr_heap),hl

            call    _ir_enable
            ld      de,#__sys_info
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _syscall_init()
            ;; ----------------------------------------------------------------
            ;; registers the OS "partos" service once and returns the service
            ;; object. later calls simply return the cached pointer.
            ;; ----------------------------------------------------------------
_syscall_init::
            call    _ir_disable
            ld      de,(_syscall_service)
            ld      a,d
            or      e
            jr      nz,sci_done$
            ld      hl,#syscall_name$
            ld      de,#_syscall_table
            call    _svc_register
            ld      (_syscall_service),de
sci_done$:
            call    _ir_enable
            ret

            .area   _INITIALIZED

_syscall_service::
            .dw     0x0000

_syscall_table::
            ;; system information
            .dw     _partos_get_sys_info

            ;; console / keyboard / launcher
            .dw     _partos_clear_screen
            .dw     _partos_set_xy
            .dw     _partos_write_console
            .dw     _partos_peek_keyboard
            .dw     _partos_read_keyboard
            .dw     _partos_run_command

            ;; intrusive lists
            .dw     _list_find
            .dw     _list_iterate
            .dw     _list_insert
            .dw     _list_remove
            .dw     _list_append

            ;; memory
            .dw     _mem_init
            .dw     _mem_allocate
            .dw     _mem_free

            ;; interrupts / vectors
            .dw     _ir_disable
            .dw     _ir_enable
            .dw     _ir_set
            .dw     _vector_set

            ;; named services
            .dw     _svc_register
            .dw     _svc_unregister
            .dw     _svc_query

            ;; events / timers
            .dw     _evt_create
            .dw     _evt_destroy
            .dw     _evt_set
            .dw     _tmr_install
            .dw     _tmr_uninstall

            ;; threads
            .dw     _thread_create
            .dw     _thread_resume
            .dw     _thread_suspend
            .dw     _thread_exit
            .dw     _thread_wait4events

            ;; processes
            .dw     _process_start
            .dw     _process_load_image
            .dw     _process_load_com
            .dw     _process_exit

            ;; filesystem
            .dw     _fat_mount_dev
            .dw     _fat_mount
            .dw     _fat_lookup
            .dw     _fat_open
            .dw     _fat_create
            .dw     _fat_read
            .dw     _fat_write
            .dw     _fat_readdir        ; userland nice name: readdir (DIR/LS)
            .dw     _partos_get_boot_fs
            .dw     _partos_get_command_line
            .dw     _fat_unlink
            .dw     _fat_mkdir
            .dw     _fat_rmdir
            .dw     _process_wait

syscall_name$:
            .db     'p','a','r','t','o','s',0

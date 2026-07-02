            ;; kernel.s
            ;;
            ;; the kernel ABI as ONE function-pointer table (_kernel_table). its
            ;; layout is the kernel_t struct in include/kernel.h -- keep the two
            ;; in lock-step. the OS reaches the table at a fixed address (pinned
            ;; when the two images link) and calls through it.
            ;;
            ;; most entries point straight at the public kernel function. only
            ;; the few that need ABI/result shaping keep a small wrapper below:
            ;; get_sys_vars, is_signalled, acquire/test_lock, bcd_to_bin and
            ;; bin_to_bcd.
            ;;
            ;; 2026-06-21   tstih
            .module kernel

            .include "../partos.inc"

            ;; --- table targets (kernel internals) ----------------------------
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _evt_set
            .globl  _list_insert
            .globl  _list_remove
            .globl  _list_find
            .globl  _list_iterate
            .globl  _list_match_eq
            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_suspend
            .globl  _thread_exit
            .globl  _thread_wait4events
            .globl  __so_create
            .globl  __so_destroy
            .globl  _set_cleanup
            .globl  _owner_cleanup_register
            .globl  _vector_set
            .globl  _vector_get
            .globl  lock_acquire
            .globl  lock_release
            .globl  lock_test
            .globl  _svc_register
            .globl  _svc_unregister
            .globl  _svc_query
            .globl  _tmr_install
            .globl  _tmr_uninstall
            .globl  __tmr_chain
            .globl  _process_start
            .globl  _process_load_image
            .globl  _process_load_com
            .globl  _process_wait
            .globl  _process_exit
            .globl  bcd_to_bin
            .globl  bin_to_bcd
            .globl  delay_1ms
            ;; appended entries (see end of _kernel_table): give the OS the last
            ;; few primitives it reaches so nothing needs a link-time address.
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set
            .globl  _list_append
            .globl  _mem_init

            ;; --- sysvars sources ---------------------------------------------
            .globl  __sys_heap
            .globl  __usr_heap
            .globl  _thread_current             ; base of the list-head block

            ;; --- public ------------------------------------------------------
            .globl  _kernel_table
            .globl  __kernel_api

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; sysvars_t *get_sys_vars(void)
            ;; ----------------------------------------------------------------
kfn_get_sys_vars$:
            ld      hl,#sysvars$
            ret

            ;; ----------------------------------------------------------------
            ;; uint8_t is_signalled(event_t *e)   <hl> e -> <a>/<hl> state
            ;; ----------------------------------------------------------------
            ;; event_t layout: +0 next, +2 owner, +4 state.
            ;; ----------------------------------------------------------------
kfn_is_signalled$:
            ld      a,l
            add     a,#4
            ld      l,a
            ld      a,h
            adc     a,#0
            ld      h,a
            ld      a,(hl)              ; a = state
            ld      l,a
            ld      h,#0
            ret

            ;; ----------------------------------------------------------------
            ;; uint8_t acquire_lock(void *lock)  <hl> -> 1 acquired / 0 busy
            ;; ----------------------------------------------------------------
kfn_acquire_lock$:
            call    lock_acquire        ; Z set on success
            ld      a,#0
            ret     nz
            inc     a                   ; a = 1 (acquired)
            ret

            ;; ----------------------------------------------------------------
            ;; uint8_t test_lock(void *lock)  <hl> -> 1 locked / 0 free
            ;; ----------------------------------------------------------------
kfn_test_lock$:
            call    lock_test           ; Z if free, NZ if locked
            ld      a,#0
            ret     z
            inc     a                   ; a = 1 (locked)
            ret

            ;; ----------------------------------------------------------------
            ;; __kernel_api : rst 0x08 handler -> hl = &_kernel_table
            ;; ----------------------------------------------------------------
            ;; the OS (and any bare payload) discovers the kernel ABI by issuing
            ;; `rst 0x08`, which lands in the low-page dispatch and ends up here.
            ;; it returns a pointer to the fixed _kernel_table, which now covers
            ;; the kernel-owned service, timer, process, bcd and delay helpers in
            ;; addition to the older scheduler/memory/vector primitives.
            ;; ----------------------------------------------------------------
__kernel_api::
            ld      hl,#_kernel_table
            ret

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; _kernel_table : the kernel ABI (matches kernel_t in kernel.h)
            ;; ----------------------------------------------------------------
_kernel_table::
            .dw     kfn_get_sys_vars$       ; get_sys_vars
            ;; memory
            .dw     _mem_allocate           ; allocate_memory
            .dw     _mem_free               ; deallocate_memory
            ;; events
            .dw     _evt_create             ; create_event
            .dw     _evt_destroy            ; destroy_event
            .dw     _evt_set                ; set_event
            .dw     kfn_is_signalled$       ; is_signalled
            ;; lists
            .dw     _list_insert            ; insert_list
            .dw     _list_remove            ; remove_list
            .dw     _list_find              ; find_list
            .dw     _list_iterate           ; iterate_list
            .dw     _list_match_eq          ; match_list_eq
            ;; threads
            .dw     _thread_create          ; create_thread
            .dw     _thread_resume          ; resume_thread
            .dw     _thread_suspend         ; suspend_thread
            .dw     _thread_exit            ; exit_thread
            .dw     _thread_wait4events     ; wait_events
            ;; system objects
            .dw     __so_create             ; create_object
            .dw     __so_destroy            ; destroy_object
            .dw     _set_cleanup            ; set_cleanup
            .dw     _owner_cleanup_register ; register_owner_cleanup
            ;; vectors
            .dw     _vector_set             ; set_vector
            .dw     _vector_get             ; get_vector
            ;; locks
            .dw     kfn_acquire_lock$       ; acquire_lock
            .dw     lock_release            ; release_lock
            .dw     kfn_test_lock$          ; test_lock
            ;; appended to preserve the older ABI offsets above
            ;; services
            .dw     _svc_register           ; register_service
            .dw     _svc_unregister         ; unregister_service
            .dw     _svc_query              ; query_service
            ;; timers
            .dw     _tmr_install            ; install_timer
            .dw     _tmr_uninstall          ; uninstall_timer
            .dw     __tmr_chain             ; chain_timers
            ;; processes
            .dw     _process_start          ; start_process
            .dw     _process_load_image     ; load_process_image
            .dw     _process_load_com       ; load_process_com
            .dw     _process_wait           ; wait_process
            .dw     _process_exit           ; exit_process
            ;; misc helpers
            .dw     kfn_bcd_to_bin$         ; bcd_to_bin
            .dw     kfn_bin_to_bcd$         ; bin_to_bcd
            .dw     delay_1ms               ; delay_1ms
            ;; appended -- MUST stay last so the offsets above never shift. these
            ;; complete the OS's kernel surface (interrupt enable/disable, IM2
            ;; device-vector install, list append, heap init) so the OS reaches
            ;; the kernel entirely through rst 0x08.
            .dw     _ir_disable             ; disable_interrupts
            .dw     _ir_enable              ; enable_interrupts
            .dw     _ir_set                 ; set_irq_vector
            .dw     _list_append            ; append_list
            .dw     _mem_init               ; initialize_memory

            ;; ----------------------------------------------------------------
            ;; sysvars_t : kernel publishes heaps, IM2 table, and the base of its
            ;; list-head block. (writable shared data.)
            ;; ----------------------------------------------------------------
            ;; the 6 kernel list heads are one contiguous block (page0.s, in the
            ;; pre-nmi pad): thread current/suspended/running/waiting/terminated
            ;; then event_list. publish the base (one pointer) and index it with
            ;; the SYSVAR_LH_* offsets in kernel.h -- the 6 individual pointers we
            ;; used to store were dead duplication (the OS reads the heads
            ;; directly, and the full all-lists view lives in the OS __sys_info).
            ;; ----------------------------------------------------------------
sysvars$:
            .dw     __sys_heap                  ; sys_heap (shared top reserve)
            .dw     __usr_heap                  ; bank1_heap (process arena)
            .dw     __usr_heap                  ; bank2_heap (same window, bank 2)
            .dw     KERNEL_IM2_BASE             ; im2_table
            .dw     _thread_current             ; list_heads: base of the 6-word block

            ;; ----------------------------------------------------------------
            ;; uint8_t bcd_to_bin(uint8_t bcd)   <a>/<hl> -> <a>/<hl>
            ;; ----------------------------------------------------------------
kfn_bcd_to_bin$:
            call    bcd_to_bin
            ld      l,a
            ld      h,#0
            ret

            ;; ----------------------------------------------------------------
            ;; uint8_t bin_to_bcd(uint8_t value)   <a>/<hl> -> <a>/<hl>
            ;; ----------------------------------------------------------------
kfn_bin_to_bcd$:
            call    bin_to_bcd
            ld      l,a
            ld      h,#0
            ret

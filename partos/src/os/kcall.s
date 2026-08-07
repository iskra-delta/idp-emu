            ;; kcall.s
            ;;
            ;; OS -> kernel call layer, discovered at boot through rst 0x08.
            ;;
            ;; the OS no longer links kernel entry points by absolute address.
            ;; instead this module owns one `jp <kernel fn>` trampoline per kernel
            ;; primitive the OS uses, and _kcall_init() patches each trampoline's
            ;; operand ONCE at boot from the kernel ABI table returned by rst 0x08
            ;; (hl = &_kernel_table). every existing OS/driver call site is byte-
            ;; identical -- it still `call _ir_disable` etc.; only the resolution
            ;; moved from a link-time equate (the generated kernel_imports.s) to a
            ;; runtime rst 0x08 lookup. cost per call is a single extra `jp`, and
            ;; because the trampoline is a tail `jp` the argument ABI (hl/de/bc and
            ;; any stacked args) passes straight through untouched.
            ;;
            ;; the trampoline block and the parallel offset table MUST stay in the
            ;; same order and in lock-step with kernel_t / _kernel_table
            ;; (include/kernel.h, src/kernel/kernel.s). the byte offsets below are
            ;; (table index * 2). the Makefile derives the kernel_imports.s
            ;; exclusion set directly from the `::` labels here, so nothing needs
            ;; to be listed twice.
            ;;
            ;; NOT routed here (kept as absolute imports on purpose):
            ;;   - bcd_to_bin / bin_to_bcd : their _kernel_table slots are ABI
            ;;     wrappers that also load hl, but rtc.s keeps hl live across the
            ;;     call, so it must reach the raw leaf helpers.
            ;;   - data heads, vector/handler addresses and one-shot init helpers
            ;;     (heaps, list heads, __svc_query_rst10, __tmr_chain, _svc_init,
            ;;     ...): not part of the callable ABI table.
            ;;
            ;; 2026-07-02   tstih
            .module kcall

            .globl  _kcall_init

            ;; trampolines -- one public symbol per routed kernel primitive.
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set
            .globl  _mem_init
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  _evt_create
            .globl  _evt_destroy
            .globl  _evt_set
            .globl  _list_find
            .globl  _list_iterate
            .globl  _list_insert
            .globl  _list_remove
            .globl  _list_append
            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_suspend
            .globl  _thread_exit
            .globl  _thread_wait4events
            .globl  _owner_cleanup_register
            .globl  _vector_set
            .globl  _svc_register
            .globl  _svc_unregister
            .globl  _svc_query
            .globl  _tmr_install
            .globl  _tmr_uninstall
            .globl  _process_start
            .globl  _process_load_image
            .globl  _process_load_com
            .globl  _process_wait
            .globl  _process_exit
            .globl  delay_1ms

            .equ    KCALL_COUNT,        32

            .area   _CODE
            ;; ----------------------------------------------------------------
            ;; _kcall_init()
            ;; ----------------------------------------------------------------
            ;; MUST run before any routed kernel call (it is the first thing
            ;; __os_entry does). resolves the kernel ABI table via rst 0x08 and
            ;; patches every trampoline's jp operand. calls NO trampoline itself.
            ;; safe with interrupts either way: at this point __os_entry is the
            ;; only payload thread and the kernel tick path never enters these
            ;; trampolines, so a half-patched block is never executed.
            ;; ----------------------------------------------------------------
_kcall_init::
            rst     0x08                ; hl = &_kernel_table
            ld      (kcall_table$),hl
            ld      hl,#kcall_off$      ; hl -> offset table (1 byte / tramp)
            ld      ix,#kcall_base$     ; ix -> first trampoline (3 bytes each)
            ld      b,#KCALL_COUNT
kci_loop$:
            ld      c,(hl)              ; c = byte offset into _kernel_table
            inc     hl
            push    hl                  ; save offset cursor
            push    bc                  ; save counter (b) + offset (c)
            ld      e,c
            ld      d,#0
            ld      hl,(kcall_table$)
            add     hl,de               ; hl -> _kernel_table[off]
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = kernel fn pointer
            ld      1(ix),e             ; patch jp operand lo
            ld      2(ix),d             ; patch jp operand hi
            pop     bc
            pop     hl
            inc     ix
            inc     ix
            inc     ix                  ; advance to next trampoline (+3)
            djnz    kci_loop$
            ret

            ;; ----------------------------------------------------------------
            ;; trampoline block -- each entry is exactly `jp 0` (3 bytes). the
            ;; operands are 0 until _kcall_init patches them. order == kcall_off$.
            ;; ----------------------------------------------------------------
kcall_base$:
_ir_disable::           jp      0x0000
_ir_enable::            jp      0x0000
_ir_set::               jp      0x0000
_mem_init::             jp      0x0000
_mem_allocate::         jp      0x0000
_mem_free::             jp      0x0000
_evt_create::           jp      0x0000
_evt_destroy::          jp      0x0000
_evt_set::              jp      0x0000
_list_find::            jp      0x0000
_list_iterate::         jp      0x0000
_list_insert::          jp      0x0000
_list_remove::          jp      0x0000
_list_append::          jp      0x0000
_thread_create::        jp      0x0000
_thread_resume::        jp      0x0000
_thread_suspend::       jp      0x0000
_thread_exit::          jp      0x0000
_thread_wait4events::   jp      0x0000
_owner_cleanup_register:: jp    0x0000
_vector_set::           jp      0x0000
_svc_register::         jp      0x0000
_svc_unregister::       jp      0x0000
_svc_query::            jp      0x0000
_tmr_install::          jp      0x0000
_tmr_uninstall::        jp      0x0000
_process_start::        jp      0x0000
_process_load_image::   jp      0x0000
_process_load_com::     jp      0x0000
_process_wait::         jp      0x0000
_process_exit::         jp      0x0000
delay_1ms::             jp      0x0000

            ;; ----------------------------------------------------------------
            ;; kcall_off$ -- _kernel_table byte offset for each trampoline above,
            ;; in the SAME order. offset = (kernel_t field index) * 2.
            ;; ----------------------------------------------------------------
kcall_off$:
            .db     80                  ; _ir_disable        (disable_interrupts)
            .db     82                  ; _ir_enable         (enable_interrupts)
            .db     84                  ; _ir_set            (set_irq_vector)
            .db     88                  ; _mem_init          (initialize_memory)
            .db     2                   ; _mem_allocate      (allocate_memory)
            .db     4                   ; _mem_free          (deallocate_memory)
            .db     6                   ; _evt_create        (create_event)
            .db     8                   ; _evt_destroy       (destroy_event)
            .db     10                  ; _evt_set           (set_event)
            .db     18                  ; _list_find         (find_list)
            .db     20                  ; _list_iterate      (iterate_list)
            .db     14                  ; _list_insert       (insert_list)
            .db     16                  ; _list_remove       (remove_list)
            .db     86                  ; _list_append       (append_list)
            .db     24                  ; _thread_create     (create_thread)
            .db     26                  ; _thread_resume     (resume_thread)
            .db     28                  ; _thread_suspend    (suspend_thread)
            .db     30                  ; _thread_exit       (exit_thread)
            .db     32                  ; _thread_wait4events (wait_events)
            .db     40                  ; _owner_cleanup_register (register_owner_cleanup)
            .db     42                  ; _vector_set        (set_vector)
            .db     52                  ; _svc_register      (register_service)
            .db     54                  ; _svc_unregister    (unregister_service)
            .db     56                  ; _svc_query         (query_service)
            .db     58                  ; _tmr_install       (install_timer)
            .db     60                  ; _tmr_uninstall     (uninstall_timer)
            .db     64                  ; _process_start     (start_process)
            .db     66                  ; _process_load_image (load_process_image)
            .db     68                  ; _process_load_com  (load_process_com)
            .db     70                  ; _process_wait      (wait_process)
            .db     72                  ; _process_exit      (exit_process)
            .db     78                  ; delay_1ms          (delay_1ms)

            .area   _SYSVARS

kcall_table$:
            .ds     2                   ; cached &_kernel_table (rst 0x08 result)

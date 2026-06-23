            ;; init.s
            ;;
            ;; early kernel entry.
            ;;
            ;; the ROM now loads the split images directly:
            ;;   sectors 1..8   -> 0x0000  micro-kernel
            ;;   sectors 9..72  -> 0xc000  OS payload
            ;; then jumps to 0x0000, whose rst 0x00 slot is `di ; jp __sys_kernel`.
            ;; from here the kernel mirrors its low page into both banks, brings
            ;; the scheduler online and starts the first payload thread at 0xc000.
            ;;
            ;; 2026-06-14   tstih
            .module init

            .include "../partos.inc"

            .globl  __sys_kernel
            .globl  __usr_heap
            .globl  __sys_heap
            .globl  __ir_init
            .globl  ir_armed
            .globl  _ir_enable
            .globl  _mem_init
            .globl  _thread_create
            .globl  _thread_resume
            .globl  __thread_robin
            .globl  __bank_copy
            .globl  __bank_copy_size
            .globl  __boot_model_hint
            .globl  __boot_model_cache

            ;; the first thread runs the payload's entry point. the payload
            ;; (services image at 0xc000) does device enumeration, the rst 0x10
            ;; service bridge, and boot/shell load -- none of which the micro-
            ;; kernel knows about. with no payload present only the idle thread
            ;; runs (see __sys_kernel below).
            .equ    PAYLOAD_THREAD_STACK,  4096
            .equ    IDLE_THREAD_STACK,      256

            ;; the boot bank threads run in -- bank 1 is the power-on/cold-start
            ;; bank. (the kernel stores this per-thread; it does not yet schedule
            ;; across banks -- that is the context-switch's job, separate code.)
            .equ    BOOT_BANK,              1

            ;; bytes mem_init writes as the block-0 free-list header (5-byte block
            ;; header + 2-byte size); copying them replicates an empty heap.
            .equ    HEAP_HDR_BYTES,         7

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; __sys_kernel()
            ;; ----------------------------------------------------------------
            ;; first shared-memory kernel instruction stream after page-0
            ;; installation. this is intentionally minimal for now: it marks
            ;; the canonical kernel entry and parks safely until the real
            ;; early-init sequence is added.
            ;; ----------------------------------------------------------------
__sys_kernel::
            di
            ld      sp,#KERNEL_STACK_TOP
            ;; preserve the ROM's model handoff byte before any later low-page
            ;; maintenance can disturb it. the OS reads the cached copy from
            ;; shared kernel sysvars instead of trusting the raw page-0 byte.
            ld      a,(__boot_model_hint)
            ld      (__boot_model_cache),a
            ;; init interrupts: point the i register at the im 2 table. the table
            ;; sits in the fixed top-of-RAM reserve (KERNEL_IM2_BASE) that BOTH
            ;; the kernel and the OS agree on, so the constant is legitimate.
            ;; _ir_set still reads the page back from the i register (no other
            ;; module bakes in the address). __ir_init also seeds ir_refcnt = 1,
            ;; so the boot `di` above IS the held disable bracket -- no separate
            ;; _ir_disable needed (the first _ir_enable at hand-off balances it).
            ld      hl,#KERNEL_IM2_BASE
            call    __ir_init
            ;; preconfigure the heaps (the stack pointer was set above). the
            ;; system heap lives in the top reserve (small kernel + OS objects);
            ;; the process arena (0x0b20..0xbfff) is the per-bank heap for thread
            ;; stacks and process images. both banks share this window; bank 2's
            ;; own RAM is initialised when banking is brought up.
            ld      hl,#__usr_heap            ; process arena (bank 1)
            ld      de,#USER_HEAP_SIZE
            call    _mem_init
            ld      hl,#__sys_heap            ; system heap (top reserve)
            ld      de,#KERNEL_HEAP_SIZE
            call    _mem_init

            ;; --- put the kernel in BOTH banks --------------------------------
            ;; interrupts are still hard-off here, which the bank flips require.
            ;; 1. relocate __bank_copy into always-mapped exec space, so it
            ;;    survives the flips while the low page is being mirrored.
            ld      hl,#__bank_copy
            ld      de,#KERNEL_EXEC_SPACE
            ld      bc,#__bank_copy_size
            ldir
            ;; 2. mirror the 2 KB micro-kernel low page into bank B (src==dst here,
            ;;    same address in both banks). a=0 selects the current A -> B
            ;;    direction; run from shared exec space so it survives the flips.
            ld      hl,#UKERNEL_LOAD_BASE        ; src 0x0000 (bank A)
            ld      de,#UKERNEL_LOAD_BASE        ; dst 0x0000 (bank B)
            ld      bc,#(UKERNEL_SECTORS * 256)  ; 2 KB
            xor     a                           ; dir = 0 -> A -> B
            call    KERNEL_EXEC_SPACE
            ;; 3. initialise bank B's process arena by replicating the block-0
            ;;    free-list header mem_init just wrote into the current arena.
            ld      hl,#USER_HEAP_BASE           ; src 0x0b20 (bank A)
            ld      de,#USER_HEAP_BASE           ; dst 0x0b20 (bank B)
            ld      bc,#HEAP_HDR_BYTES
            xor     a                           ; dir = 0 -> A -> B
            call    KERNEL_EXEC_SPACE

            ;; the kernel ABI table needs no registration: the OS discovers it
            ;; with `rst 0x08` (-> hl = &_kernel_table). service moved to the OS.
            ;; the scheduler tick is the rst 0x18 vector (page0.s -> __thread_robin);
            ;; the kernel installs NO timer of its own. with no tick source the
            ;; system is cooperative (single-tasking); preemption begins only when
            ;; the OS routes a timer interrupt at rst 0x18.

            ;; create the kernel idle thread first. it guarantees the running
            ;; list is never empty, so the scheduler always has something to
            ;; dispatch. it is what the cpu runs when every other thread is
            ;; blocked: it simply halts until an interrupt. once the OS has
            ;; wired the periodic IM2 tick, that interrupt path already enters
            ;; the scheduler, so the idle loop itself must stay passive.
            ld      hl,#__sys_idle
            ld      de,#IDLE_THREAD_STACK
            ld      bc,#0x0000
            push    bc                  ; thread_data = NONE
            ld      bc,#BOOT_BANK
            push    bc                  ; bank = boot bank
            call    _thread_create     ; callee-clean: pops both stacked args
            ld      a,d
            or      e
            jr      z,__sys_kernel_halt$   ; cannot even create idle -> dead halt
            ex      de,hl
            call    _thread_resume

            ;; create the first payload thread = the OS entry point. the payload
            ;; brings up devices, the rst 0x10 service bridge and the boot/shell
            ;; loader from its own init thread; the micro-kernel just schedules
            ;; it. PAYLOAD_ENTRY is the fixed services base (0xc000). with no
            ;; payload loaded only the idle thread runs.
            ld      hl,#PAYLOAD_ENTRY
            ld      de,#PAYLOAD_THREAD_STACK
            ld      bc,#0x0000
            push    bc                  ; thread_data = NONE
            ld      bc,#BOOT_BANK
            push    bc                  ; bank = boot bank
            call    _thread_create     ; callee-clean: pops both stacked args
            ld      a,d
            or      e
            jr      z,__sys_kernel_sched$
            ex      de,hl
            call    _thread_resume

__sys_kernel_sched$:
            ;; enable IM 2 and balance the early _ir_disable, then hand control to
            ;; the scheduler. the FIRST dispatch happens INSIDE __thread_robin: it
            ;; selects the first runnable thread and reti's into it (the kernel's
            ;; init stack is abandoned). everything above ran with interrupts off.
            im      2
            ld      a,#1
            ld      (ir_armed),a
            call    _ir_enable          ; refcnt 1 -> 0, ei (balances init's di)
            jp      __thread_robin      ; first dispatch -> first runnable thread

__sys_kernel_halt$:
            di
            halt
            jr      __sys_kernel_halt$

            ;; ----------------------------------------------------------------
            ;; __sys_idle -- the kernel idle thread
            ;; ----------------------------------------------------------------
            ;; runs only when no other thread is runnable. halts until an
            ;; interrupt (e.g. the periodic timer tick or an i/o completion that
            ;; wakes another thread). the interrupt path itself performs any
            ;; required scheduling; the idle loop just goes back to sleep.
            ;; dispatched with interrupts enabled (reti), so halt waits with
            ;; interrupts on.
            ;; ----------------------------------------------------------------
__sys_idle:
__sys_idle$:
            halt
            jr      __sys_idle$

            .area   _SYSVARS

__boot_model_cache::
            .ds     1

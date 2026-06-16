            ;; init.s
            ;;
            ;; early kernel entry. rom bootstrap loads the kernel image to its
            ;; final address in common ram, sets hl to __sys_kernel, then jumps
            ;; to __sys_page0_install. once low page is installed into both
            ;; banks, control returns here in logical bank 0.
            ;;
            ;; 2026-06-14   tstih
            .module init

            .include "../partos.inc"

            .globl  __sys_kernel
            .globl  __sys_heap
            .globl  _ir_init
            .globl  _ir_disable
            .globl  _mem_init

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
            ld      sp,#0xffff
            call    _ir_init
            call    _ir_disable
            ld      hl,#__sys_heap
            ld      de,#KERNEL_HEAP_SIZE
            call    _mem_init

__sys_kernel_idle$:
            halt
            jr      __sys_kernel_idle$

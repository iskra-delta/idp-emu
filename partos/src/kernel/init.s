            ;; init.s
            ;;
            ;; early kernel entry. the current rom loads an os image into
            ;; 0xe000..0xffff, jumps to __sys_page0_install at 0xff6b, and
            ;; currently passes hl=0xe000 as the continuation address. once
            ;; low page is installed into both banks, control eventually lands
            ;; in the loaded image continuation path and may reach here.
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

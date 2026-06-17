            ;; heap.s
            ;;
            ;; reserved top-of-common-ram heap for the kernel. this area now
            ;; sits below a small fixed kernel stack and the top-of-memory im 2
            ;; table, while the page-0 installer block lives one page below the
            ;; heap:
            ;;
            ;;   ... kernel code/data ...
            ;;   0xf900..0xf9ff  kernel page0 block  (256 bytes)
            ;;   0xfa00..0xfd7f  kernel heap         (896 bytes)
            ;;   0xfd80..0xfdff  kernel stack        (128 bytes)
            ;;   0xfe00..0xffff  kernel im 2 table   (512 bytes)
            ;;
            ;; 2026-06-14   tstih
            .module heap

            .include "../partos.inc"

            .globl  __usr_heap
            .globl  __usr_heap_end
            .globl  __sys_heap
            .globl  __sys_heap_end

            .equ    __usr_heap,      USER_HEAP_BASE
            .equ    __usr_heap_end,  USER_HEAP_TOP

            .area   _HEAP

__sys_heap::
            .ds     KERNEL_HEAP_SIZE
__sys_heap_end::

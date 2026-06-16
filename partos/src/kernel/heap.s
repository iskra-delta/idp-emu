            ;; heap.s
            ;;
            ;; reserved top-of-common-ram heap for the kernel. this area is
            ;; linked below the dedicated im 2 vector page and final kernel
            ;; page so the memory map is:
            ;;
            ;;   ... kernel code/data ...
            ;;   0xf800..0xfdff  kernel heap         (1536 bytes)
            ;;   0xfe00..0xfeff  kernel im 2 table   (256 bytes)
            ;;   0xff00..0xffff  kernel page 0/stack (256 bytes)
            ;;
            ;; 2026-06-14   tstih
            .module heap

            .include "../partos.inc"

            .globl  __sys_heap
            .globl  __sys_heap_end

            .area   _HEAP

__sys_heap::
            .ds     KERNEL_HEAP_SIZE
__sys_heap_end::

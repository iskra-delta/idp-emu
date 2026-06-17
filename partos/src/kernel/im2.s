            ;; im2.s
            ;;
            ;; reserved top-of-common-ram im 2 vector table for the kernel.
            ;; it spans the final two pages of memory. the kernel stack and heap
            ;; now live directly below it, and the table stays page-aligned
            ;; because the cpu forms each handler address as (i << 8) | vector.
            ;;
            ;;   ... kernel code/data ...
            ;;   0xf900..0xf9ff  kernel page0 block  (256 bytes)
            ;;   0xfa00..0xfd7f  kernel heap         (896 bytes)
            ;;   0xfd80..0xfdff  kernel stack        (128 bytes)
            ;;   0xfe00..0xffff  kernel im 2 table   (512 bytes)
            ;;
            ;; 2026-06-14   tstih
            .module im2

            .include "../partos.inc"

            .globl  __sys_im2
            .globl  __sys_im2_end

            .area   _IM2

__sys_im2::
            .ds     KERNEL_IM2_SIZE
__sys_im2_end::

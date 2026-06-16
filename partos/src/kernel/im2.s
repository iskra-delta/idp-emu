            ;; im2.s
            ;;
            ;; reserved top-of-common-ram im 2 vector page for the kernel.
            ;; this page sits between the system heap and the final kernel
            ;; page/stack block.
            ;;
            ;;   ... kernel code/data ...
            ;;   0xf800..0xfdff  kernel heap         (1536 bytes)
            ;;   0xfe00..0xfeff  kernel im 2 table   (256 bytes)
            ;;   0xff00..0xffff  kernel page 0/stack (256 bytes)
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

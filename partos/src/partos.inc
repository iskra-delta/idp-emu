            ;; partos.inc
            ;;
            ;; partos memory map (see docs/PARTOS-VOLUME-1-ROM.md and
            ;; docs/PARTOS-VOLUME-2-KERNEL.md). these are addresses only; all
            ;; sizes are computed by the linker (area symbols) and the build
            ;; (link map).
            ;;
            ;; 2026-06-11   tstih

            ;; --- rom ----------------------------------------------------------
            .equ    BOOT_BASE,      0x0000 ; bootstrap, runs from rom in place
            .equ    STAGE1_BASE,    0x2000 ; decompressed stage-1 bios entry

            ;; --- runtime memory map ------------------------------------------
            .equ    BIOS_BASE,      0xf600 ; bios runtime location (mem top)
            ;; bios variables must stay BELOW the 8 KB OS load window
            ;; (0xe000-0xffff) and the boot record staging at 0xdf00, or the
            ;; boot loader would overwrite its own state while loading the OS.
            .equ    SYSVARS_BASE,   0xde00 ; bios variables (256 bytes)
            .equ    HEAP_BOTTOM,    0xfe00 ; legacy bios heap floor
            .equ    HEAP_TOP,       0xffff ; highest heap byte (top of ram)
            .equ    STACK_TOP,      BIOS_BASE ; bios/stage-1 stack grows below bios
            ;; user processes share one common heap below the linked kernel
            ;; image. reserve everything above page 0 and below 0xc000 so
            ;; application images and their private stacks all come from this
            ;; pool while still leaving the kernel heap isolated up top.
            .equ    USER_HEAP_BASE,     0x0100
            .equ    USER_HEAP_SIZE,     0xbf00
            .equ    USER_HEAP_TOP,      0xc000

            ;; --- kernel runtime map ------------------------------------------
            ;; keep the kernel runtime reserve to 1.5 KB total for now:
            ;;   0xfa00..0xfd7f  kernel heap         (896 bytes)
            ;;   0xfd80..0xfdff  kernel stack        (128 bytes)
            ;;   0xfe00..0xffff  kernel im 2 table   (512 bytes)
            ;; page-0 install code lives just below that reserve.
            .equ    KERNEL_HEAP_BASE,   0xfa00
            .equ    KERNEL_HEAP_SIZE,   0x0380
            .equ    KERNEL_STACK_BASE,  0xfd80
            .equ    KERNEL_STACK_SIZE,  0x0080
            .equ    KERNEL_STACK_TOP,   0xfe00
            ;; im 2 table must be page-aligned (the i register supplies its high
            ;; byte). a device vector V selects the 2-byte handler pointer at
            ;; base + V, so the full vector range spans 512 bytes / two pages.
            .equ    KERNEL_IM2_BASE,    0xfe00 ; 512-byte IM 2 vector table
            .equ    KERNEL_IM2_SIZE,    0x0200
            .equ    KERNEL_PAGE0_BASE,  0xf900 ; page-0 image + installer block
            .equ    KERNEL_PAGE0_SIZE, 0x0100
            .equ    KERNEL_PAGE0_INSTALL, 0xf96b

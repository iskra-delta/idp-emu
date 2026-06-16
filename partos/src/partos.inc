            ;; partos.inc
            ;;
            ;; partos memory map (see docs/partos/ARCHITECTURE.md). these are
            ;; addresses only; all sizes are computed by the linker (area
            ;; symbols) and the build (link map).
            ;;
            ;; 2026-06-11   tstih

            ;; --- rom ----------------------------------------------------------
            .equ    BOOT_BASE,      0x0000 ; bootstrap, runs from rom in place
            .equ    STAGE1_BASE,    0x0800 ; decompressed stage-1 bios entry
            .equ    BOOT_SCRATCH_BASE, 0xb000 ; ram trampoline/decoder workspace
            .equ    BOOT_SCRATCH_DZX0, 0xb020 ; copied zx0 decoder entry
            .equ    BOOT_SCRATCH_DATA, 0xb080 ; copied compressed stage-1 blob

            ;; --- runtime memory map ------------------------------------------
            .equ    BIOS_BASE,      0xf600 ; bios runtime location (mem top)
            ;; bios variables must stay BELOW the 8 KB OS load window
            ;; (0xe000-0xffff) and the boot record staging at 0xdf00, or the
            ;; boot loader would overwrite its own state while loading the OS.
            .equ    SYSVARS_BASE,   0xde00 ; bios variables (256 bytes)
            .equ    HEAP_BOTTOM,    0xfe00 ; lowest heap byte (512 byte heap)
            .equ    HEAP_TOP,       0xffff ; highest heap byte (top of ram)
            .equ    STACK_TOP,      BIOS_BASE ; sp grows down below the bios

            ;; --- kernel runtime map ------------------------------------------
            .equ    KERNEL_HEAP_BASE,  0xf800 ; 1.5 KB os heap at mem top
            .equ    KERNEL_HEAP_SIZE,  0x0600
            .equ    KERNEL_IM2_BASE,   0xfe00 ; 256-byte IM 2 vector table
            .equ    KERNEL_IM2_SIZE,   0x0100
            .equ    KERNEL_PAGE0_BASE, 0xff00 ; final kernel page / early stack
            .equ    KERNEL_PAGE0_SIZE, 0x0100

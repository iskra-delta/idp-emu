            ;; partos.inc
            ;;
            ;; partos memory map (see docs/partos/ARCHITECTURE.md). these are
            ;; addresses only; all sizes are computed by the linker (area
            ;; symbols) and the build (link map).
            ;;
            ;; 2026-06-11   tstih

            ;; --- rom ----------------------------------------------------------
            .equ    BOOT_BASE,      0x0100 ; boot, runs from rom in place

            ;; --- runtime memory map ------------------------------------------
            .equ    BIOS_BASE,      0xf600 ; bios runtime location (mem top)
            .equ    SYSVARS_BASE,   0xfd00 ; bios variables (256 bytes)
            .equ    HEAP_BOTTOM,    0xfe00 ; lowest heap byte (512 byte heap)
            .equ    HEAP_TOP,       0xffff ; highest heap byte (top of ram)
            .equ    STACK_TOP,      BIOS_BASE ; sp grows down below the bios

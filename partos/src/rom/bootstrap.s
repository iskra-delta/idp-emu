            ;; bootstrap.s
            ;;
            ;; tiny rom-resident stage-0 loader.
            ;;
            ;; this is the only code linked at 0x0000 together with the zx0
            ;; decoder. everything else in the bios is linked for execution at
            ;; 0x2000, compressed, and appended immediately after this
            ;; bootstrap image in the physical 2 KB rom.
            ;;
            ;; 2026-06-14   tstih
            .module bootstrap

            .include "../partos.inc"

            .globl  bootstrap_payload
            .globl  dzx0_standard

            .equ    BOOT_STACK_TOP,      0xbfff
            .equ    DZX0_STANDARD_SIZE,  68

            .area   _BOOT

bootstrap_main::
            ;; keep stage-0 fully deterministic while we still execute from rom
            di
            ld      sp,#BOOT_STACK_TOP

            ;; keep the decoder in rom and expand stage-1 above the rom overlay
            ;; window. stage-1 itself disables the overlay as its first act
            ;; once execution has reached safe ram at STAGE1_BASE.
            ld      hl,#bootstrap_payload
            ld      de,#STAGE1_BASE
            call    dzx0_standard
            jp      STAGE1_BASE

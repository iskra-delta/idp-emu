            ;; page0.s
            ;;
            ;; page 0: z80 reserved entry points (rst 0x00..0x38, nmi at
            ;; 0x66) and the rom-side cold boot init. this page is both the
            ;; rom reset entry and the template later installed into both
            ;; ram banks. the rst/nmi slots hold safe inline defaults for
            ;; now; bios.s (to come) will repoint them to the bios jump
            ;; table during installation.
            ;;
            ;; 2026-06-11   tstih
            .module page0

            .include "../partos.inc"

            .globl  boot_main           ; defined in boot/boot.s
            .globl  s__BOOT             ; linker: boot area rom address
            .globl  l__BOOT             ; linker: boot area length
            .globl  s__CODE             ; linker: bios runtime address
            .globl  l__CODE             ; linker: bios length

            .area   _PAGE0 (ABS)

            ;; --- rst 0x00: reset / cold boot ---------------------------------
            .org    0x0000
            di
            jp      rom_init$

            ;; --- rst 0x08..0x30: unused, return ------------------------------
            .org    0x0008
            ret
            .org    0x0010
            ret
            .org    0x0018
            ret
            .org    0x0020
            ret
            .org    0x0028
            ret
            .org    0x0030
            ret

            ;; --- rst 0x38: im 1 interrupt entry --------------------------------
            .org    0x0038
            ei
            reti

            ;; --- nmi entry ------------------------------------------------------
            .org    0x0066
            retn

            ;; ----------------------------------------------------------------
            ;; rom_init$()
            ;; ----------------------------------------------------------------
            ;; rom-side cold boot. copies the bios image from the rom (it is
            ;; stored right after the boot area) to its runtime location
            ;; below the sbrk heap, then enters boot, which keeps running
            ;; from the rom in place. source, destination and length all
            ;; come from linker area symbols -- no size constants.
            ;; ----------------------------------------------------------------
            .org    0x0070
rom_init$:
            ld      sp,#STACK_TOP       ; stack below the bios, common ram
            ld      hl,#s__BOOT
            ld      bc,#l__BOOT
            add     hl,bc               ; hl = bios image location in rom
            ld      de,#s__CODE         ; bios runtime location (mem top)
            ld      bc,#l__CODE         ; bios length (from the linker)
            ldir
            jp      boot_main           ; boot runs from rom, in place

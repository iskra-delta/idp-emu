            ;; bootstrap.s
            ;;
            ;; tiny rom-resident stage-0 loader.
            ;;
            ;; this is the only code linked at 0x0000 together with the zx0
            ;; decoder. everything else in the bios is linked for execution at
            ;; 0x0800, compressed, and appended immediately after this
            ;; bootstrap image in the physical 2 KB rom.
            ;;
            ;; 2026-06-14   tstih
            .module bootstrap

            .include "../partos.inc"

            .globl  bootstrap_payload
            .globl  dzx0_standard

            .equ    BOOT_STACK_TOP,      0xbfff
            .equ    DZX0_STANDARD_SIZE,  68
            .equ    ROM_WINDOW_SIZE,     0x0800

            .area   _BOOT

bootstrap_main::
            ;; keep stage-0 fully deterministic while we still execute from rom
            di
            ld      sp,#BOOT_STACK_TOP

            ;; copy the tiny ram trampoline that will disable rom and jump into
            ;; the copied decoder. this must execute from ram because the rom
            ;; overlay disappears immediately after the bank-control write.
            ld      hl,#bootstrap_trampoline$
            ld      de,#BOOT_SCRATCH_BASE
            ld      bc,#(bootstrap_trampoline_end$ - bootstrap_trampoline$)
            ldir

            ;; copy the zx0 decoder itself into ram. the decoder is position
            ;; independent except for its four absolute internal call targets,
            ;; which we patch just below for the scratch-ran copy.
            ld      hl,#dzx0_standard
            ld      de,#BOOT_SCRATCH_DZX0
            ld      bc,#DZX0_STANDARD_SIZE
            ldir

            ;; retarget the decoder's absolute internal calls to the copied
            ;; scratch addresses:
            ;;   +0x08/+0x10 -> elias      (+0x35)
            ;;   +0x20       -> elias_loop (+0x36)
            ;;   +0x30       -> backtrack  (+0x3d)
            ld      bc,#(BOOT_SCRATCH_DZX0 + 0x35)
            ld      hl,#(BOOT_SCRATCH_DZX0 + 0x08)
            ld      (hl),c
            inc     hl
            ld      (hl),b
            ld      hl,#(BOOT_SCRATCH_DZX0 + 0x10)
            ld      (hl),c
            inc     hl
            ld      (hl),b

            ld      bc,#(BOOT_SCRATCH_DZX0 + 0x36)
            ld      hl,#(BOOT_SCRATCH_DZX0 + 0x20)
            ld      (hl),c
            inc     hl
            ld      (hl),b

            ld      bc,#(BOOT_SCRATCH_DZX0 + 0x3d)
            ld      hl,#(BOOT_SCRATCH_DZX0 + 0x30)
            ld      (hl),c
            inc     hl
            ld      (hl),b

            ;; stage the compressed payload in upper ram as well. while rom is
            ;; still enabled we can read its mirrored contents, but once we
            ;; turn rom off the source must already be in writable memory.
            ld      hl,#bootstrap_payload
            ld      de,#BOOT_SCRATCH_DATA
            ld      bc,#ROM_WINDOW_SIZE
            ldir

            jp      BOOT_SCRATCH_BASE

bootstrap_trampoline$:
            xor     a
            out     (0x80),a
            ld      hl,#BOOT_SCRATCH_DATA
            ld      de,#STAGE1_BASE
            call    BOOT_SCRATCH_DZX0
            jp      STAGE1_BASE
bootstrap_trampoline_end$:

            ;; partos.inc
            ;;
            ;; partos memory + load map (see docs/PARTOS-VOLUME-1-ROM.md and
            ;; docs/PARTOS-VOLUME-2-KERNEL.md). these are addresses only; all
            ;; sizes are computed by the linker (area symbols) and the build
            ;; (link map). entries are grouped: common, bios, kernel, os.
            ;;
            ;; 2026-06-11   tstih

            ;; ================================================================
            ;; common -- the shared image load map
            ;; ================================================================
            ;; the rom loader places two reserved disk regions into RAM, then
            ;; hands off to the OS payload. fixed bases => both images link at
            ;; constant addresses (no auto-derived load base, no layout drift).
            ;;   kernel  2 KB  -> 0x0000  micro-kernel (mirrored into both banks)
            ;;   OS     16 KB  -> 0xc000  shared payload (top reserved for kernel)
            .equ    UKERNEL_LOAD_BASE,  0x0000 ; micro-kernel, mirrored both banks
            .equ    UKERNEL_SECTORS,    8      ; 2 KB / 256
            .equ    SERVICES_LOAD_BASE, 0xc000 ; shared OS payload (16 KB)
            .equ    SERVICES_SECTORS,   64     ; 16 KB / 256

            ;; OS bank-local scratch lives in the first 1 KB above the mirrored
            ;; kernel. it holds zero-initialized OS/private driver work state
            ;; so the shared OS image can stay below the kernel reserve while
            ;; keeping its loaded code+data contiguous.
            .equ    OS_SYSVARS_BASE,    0x0800
            .equ    OS_SYSVARS_SIZE,    0x0400 ; 1 KB reserved in bank 1

            ;; a dedicated bank-local ISR stack lives just above the OS
            ;; scratch window so driver interrupts never collide with the
            ;; small common-RAM kernel heap. the process arena starts above it
            ;; and runs up to the shared region; thread stacks and process
            ;; images come from there in either bank.
            .equ    USER_HEAP_BASE,     0x0d00 ; 0x0800..0x0cff reserved
            .equ    USER_HEAP_SIZE,     0xb300 ; 0x0d00..0xbfff
            .equ    USER_HEAP_TOP,      0xc000

            ;; ram bank-select ports: any i/o access in 0x88..0x8f maps bank A,
            ;; 0x90..0x97 maps bank B (value-independent). the two differ by
            ;; BANK_TOGGLE, so a copy routine derives the "restore" port from the
            ;; destination port (see bank.s).
            .equ    BANK_A_PORT,        0x88
            .equ    BANK_B_PORT,        0x90
            .equ    BANK_TOGGLE,        (BANK_A_PORT ^ BANK_B_PORT) ; 0x18

            ;; ================================================================
            ;; bios (rom)
            ;; ================================================================
            .equ    BOOT_BASE,      0x0000 ; bootstrap, runs from rom in place
            .equ    STAGE1_BASE,    0x2000 ; decompressed stage-1 bios entry
            .equ    BIOS_BASE,      0xf600 ; bios runtime location (mem top)
            .equ    SYSVARS_BASE,   0xde00 ; bios variables (256 bytes)
            .equ    BOOT_RECORD_BUF, 0x1800 ; transient boot-sector scratch

            ;; ================================================================
            ;; kernel -- fixed top-of-RAM runtime reserve
            ;; ================================================================
            ;; the top of common RAM holds the kernel's mutable runtime state. it
            ;; is ALWAYS-MAPPED shared RAM, set up by the kernel at boot, and is
            ;; NOT part of any loaded image. BOTH the kernel and the OS know this
            ;; layout: the OS payload must stay below KERNEL_SYSVARS_BASE. the low
            ;; micro-kernel page (0x0000) holds no mutable data, so it stays
            ;; read-only / mirrored. sizes are provisional -- bump them here (and
            ;; the OS automatically gets less room) if the kernel needs more.
            ;;   0xfc20..0xfc3f  exec space (bank fns)      (32 bytes)
            ;;   0xfc40..0xfc5f  sysvars (scratch)          (32 bytes)
            ;;   0xfc60..0xfd9f  system heap (shared objects only) (320 bytes)
            ;;   0xfda0..0xfdff  kernel bootstrap / ISR stack (96 bytes)
            ;;   0xfe00..0xffff  im 2 table (page-aligned) (512 bytes)
            .equ    KERNEL_SYSVARS_BASE, 0xfc40
            ;; exec space: a few small kernel routines that MUST run from
            ;; always-mapped shared RAM -- the bank block-copy and the bank-call
            ;; trampoline. they cannot live in the mirrored low page because they
            ;; are what builds the mirror (and they flip banks under their own
            ;; PC). the kernel copies them here from its image at boot. this is
            ;; the lowest reserve address, so it is the OS payload's ceiling.
            .equ    KERNEL_EXEC_SIZE,   0x0020 ; 32 bytes; __bank_copy uses 31
            .equ    KERNEL_EXEC_SPACE,  (KERNEL_SYSVARS_BASE - KERNEL_EXEC_SIZE)
            .equ    KERNEL_HEAP_BASE,   0xfc68
            .equ    KERNEL_STACK_TOP,   0xfe00
            .equ    KERNEL_HEAP_SIZE,   0x0138 ; 312 bytes; leaves 96 B above it
            ;; device ISRs switch to the dedicated bank-local stack just below
            ;; USER_HEAP_BASE, which gives them 256 bytes of headroom without
            ;; touching the common-RAM heap.
            .equ    DRV_ISR_STACK_TOP,  USER_HEAP_BASE
            ;; im 2 table must be page-aligned (the i register supplies its high
            ;; byte). a device vector V selects the handler at base + V, so the
            ;; full vector range spans 512 bytes / two pages.
            .equ    KERNEL_IM2_BASE,    0xfe00
            .equ    KERNEL_IM2_SIZE,    0x0200

            ;; ================================================================
            ;; os -- payload entry points (all = SERVICES_LOAD_BASE)
            ;; ================================================================
            .equ    SERVICES_ENTRY,     SERVICES_LOAD_BASE ; rom hands off here
            .equ    PAYLOAD_ENTRY,      SERVICES_LOAD_BASE ; kernel first-thread start

            ;; kernel low-page boot-hint bytes (written by the ROM once the
            ;; split images are in place, before control jumps to page 0).
            .equ    KERNEL_BOOT_VERSION_ADDR, 0x0004
            .equ    KERNEL_BOOT_MODEL_ADDR,   0x0005
            .equ    KERNEL_BOOT_FLAGS_ADDR,   0x0006
            .equ    KERNEL_BOOT_META1_ADDR,   0x0007

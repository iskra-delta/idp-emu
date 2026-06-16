            ;; start.s
            ;;
            ;; stage-1 bios entry point.
            ;;
            ;; bootstrap.s inflates the compressed stage-1 image to 0x2000 and
            ;; jumps here. from this point on we are executing from ram.
            ;;
            ;; 2026-06-11   tstih
            .module start

            .include "../drivers/rtc.inc"

            .equ    BOOT_BANNER_CRT_X,   34
            .equ    BOOT_BANNER_CRT_Y,   12
            .equ    BOOT_BANNER_GDP_MODE,0x00
            .equ    BOOT_BANNER_GDP_X,   34
            .equ    BOOT_BANNER_GDP_Y,   13
            .equ    BOOT_NODEV_CRT_X,    33
            .equ    BOOT_NODEV_GDP_X,    33
            .equ    BOOT_SETUP_KEY,      0xfe
            .equ    BOOT_SETUP_SECONDS,  3
            .equ    OS_LOAD_BASE,        0xe000 ; top 8 KB of shared ram
            .equ    OS_RESERVED_SECTORS, 32     ; 8 KB OS image after boot record
            .globl  model_detect
            .globl  model
            .globl  bios_main
            .globl  bios_nvram_read
            .globl  bios_nvram_sum
            .globl  bios_nvram_cache
            .globl  bios_save_settings
            .globl  kbd_init
            .globl  kbd_read
            .globl  print_at
            .globl  avdc_set_mode
            .globl  fd_init
            .globl  fd_read_lba
            .globl  hd_read_lba
            .globl  hd_get_sda_type_index
            .globl  hd_init_chars
            .globl  boot_fd_path
            .globl  boot_hd_path

            ;; __sys_page0_install lives in the kernel's _PAGE0 segment, which
            ;; is pinned at 0xff00, so its entry is at a fixed address the BIOS
            ;; reaches in the just-loaded OS image. (kernel.map: 0xff6b; the
            ;; kernel build keeps _PAGE0 exactly 256 bytes.)
            .equ    SYS_PAGE0_INSTALL,   0xff6b

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; start_main()
            ;; ----------------------------------------------------------------
            ;; entered from bootstrap.s after the compressed stage-1 bios has
            ;; been expanded to 0x2000. bootstrap.s established the early
            ;; stack but left the rom overlay enabled; stage-1 disables the
            ;; overlay first, then continues with hardware/model setup.
            ;; model_detect leaves the result in a for later use:
            ;;   0x00 = plain text partner
            ;;   0x01 = graphics-capable partner
            ;; ----------------------------------------------------------------
start_main::
            xor     a
            out     (0x80),a

            ;; cache the detected machine class for later boot decisions
            call    model_detect
            ld      (model),a

            ;; snapshot rtc-backed setup once so menu/boot code can reuse it
            ld      hl,#bios_nvram_cache
            call    bios_nvram_read

            ;; validate the cached setup once. on a bad checksum (nibble sum
            ;; != 0), write the factory defaults (the boot-time menu state
            ;; bytes) back into NVRAM so every later reader can trust the block.
            ld      hl,#bios_nvram_cache
            call    bios_nvram_sum
            or      a
            call    nz,bios_save_settings

            ;; bring the GDP text profile up once (graphics models only) so
            ;; every banner/message below lands on the graphics output too.
            ld      a,(model)
            or      a
            jr      z,sm_banner$
            call    avdc_set_mode       ; arg ignored (80-col only); resets AVDC

sm_banner$:
            ;; centered sign-of-life banner on the active text output(s).
            ld      hl,#boot_banner$
            call    print_banner$

            ;; keep a short startup setup window before normal boot continues.
            call    kbd_init
            in      a,(RTC_PORT_SEC)
            ld      c,a

            ;; count whole rtc second transitions. the first counted edge only
            ;; aligns to the second boundary, so the effective window stays at
            ;; roughly three to four seconds on a real machine.
            ld      b,#BOOT_SETUP_SECONDS + 1

sm_wait$:
            call    kbd_read
            cp      #BOOT_SETUP_KEY
            jp      z,bios_main
            in      a,(RTC_PORT_SEC)
            cp      c
            jr      z,sm_wait$
            ld      c,a
            djnz    sm_wait$
            ;; window expired without SETUP -> load the operating system.

            ;; ----------------------------------------------------------------
            ;; boot_main: load+launch the OS, floppy first then hard disk
            ;; ----------------------------------------------------------------
boot_main$:
            ld      hl,#msg_booting$
            call    print_banner$

            ;; try floppy fd0 first; fd_init does specify + motor + recal and
            ;; fails if the controller is dead. an empty drive simply fails the
            ;; read inside boot_device$, so we still fall through to the disk.
boot_fd_path::
            xor     a                   ; unit 0
            call    fd_init
            jr      nz,bt_hd$
            xor     a                   ; device 0 = floppy
            call    boot_device$
            jr      nc,bt_go$

bt_hd$:
            ;; initialise the configured sda geometry so the controller can
            ;; read it, then try to boot from the hard disk.
boot_hd_path::
            call    hd_get_sda_type_index
            call    hd_init_chars
            ld      a,#0x01             ; device 1 = hard disk
            call    boot_device$
            jr      c,bt_nodev$

bt_go$:
            ;; hand off to the kernel: HL = load base, B = model byte. the
            ;; page-0 installer copies low page into both banks and jumps to HL.
            ld      hl,#OS_LOAD_BASE
            ld      a,(model)
            ld      b,a
            jp      SYS_PAGE0_INSTALL

bt_nodev$:
            ld      hl,#msg_nodev$
            call    print_nodev$
bt_halt$:
            halt
            jr      bt_halt$

            ;; ----------------------------------------------------------------
            ;; boot_device(<a> device) -> read+verify boot record, load OS
            ;; ----------------------------------------------------------------
            ;; device: 0 = floppy, 1 = hard disk. reads the boot record, checks
            ;; the boot signature, then streams blocks 1..32 (the reserved area)
            ;; into OS_LOAD_BASE. out: carry set on read error or no signature.
            ;; ----------------------------------------------------------------
boot_device$:
            ld      (boot_dev$),a
            ;; read blocks 0..32 contiguously: boot record lands at 0xdf00, the
            ;; 8 KB OS image at OS_LOAD_BASE. one loop covers both.
            ld      hl,#OS_LOAD_BASE - 0x100
            ld      bc,#0x2100
bd_loop$:
            push    bc
            push    hl
            ld      a,c
            call    boot_read$
            pop     hl
            pop     bc
            jr      nz,bd_fail$
            inc     h                   ; advance dst by one 256-byte sector
            inc     c
            djnz    bd_loop$

            ;; verify the boot signature (0x55 0xAA) in the loaded boot record
            ld      hl,#OS_LOAD_BASE - 0x100 + 254
            ld      a,(hl)
            cp      #0x55
            jr      nz,bd_fail$
            inc     hl
            ld      a,(hl)
            cp      #0xaa
            jr      nz,bd_fail$
            or      a                   ; carry clear = booted
            ret
bd_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; print_banner(<hl> *text) -> print at the banner cell, attr 0
            ;; ----------------------------------------------------------------
print_banner$:
            ld      bc,#0x220c
            ld      a,(model)
            or      a
            jr      z,pb_go$
            ld      c,#BOOT_BANNER_GDP_Y
pb_go$:
            xor     a
            jp      print_at            ; tail call

            ;; print_nodev(<hl> *text) -> print the no-boot-device banner
print_nodev$:
            ld      bc,#0x210c
            ld      a,(model)
            or      a
            jr      z,pn_go$
            ld      b,#BOOT_NODEV_GDP_X
            ld      c,#BOOT_BANNER_GDP_Y
pn_go$:
            xor     a
            jp      print_at            ; tail call

            ;; boot_read(<a> lba, <hl> dst): dispatch to the booted device
boot_read$:
            ld      e,a                 ; save lba across the table fetch
            ld      a,(boot_dev$)
            or      a
            ld      a,e
            jp      z,fd_read_lba
            jp      hd_read_lba

boot_banner$:
            .db     'P','A','R','T','O','S',0
msg_booting$:
            .db     'B','O','O','T','I','N','G',0
msg_nodev$:
            .db     'N','O',' ','B','O','O','T',' ','D','E','V','I','C','E',0

            .area   _SYSVARS

boot_dev$:
            .db     0x00

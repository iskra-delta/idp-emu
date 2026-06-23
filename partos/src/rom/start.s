            ;; start.s
            ;;
            ;; stage-1 bios entry point.
            ;;
            ;; bootstrap.s inflates the compressed stage-1 image to 0x2000 and
            ;; jumps here. from this point on we are executing from ram.
            ;;
            ;; 2026-06-11   tstih
            .module start

            .include "../partos.inc"
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
            .globl  fd_n$
            .globl  hd_read_lba
            .globl  hd_get_sda_type_index
            .globl  hd_init_chars
            .globl  boot_fd_path
            .globl  boot_hd_path
            .globl  boot_dev$

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
            ld      a,#1
            ld      (fd_n$),a           ; hd is 256-byte: boot ssh = 1
            call    boot_device$        ; a = 1 = device (hard disk)
            jr      c,bt_nodev$

bt_go$:
            ;; hand off to the micro-kernel entry at page 0. the loader cached
            ;; the boot metadata in the kernel's dead page-0 bytes immediately
            ;; after the 2 KB micro-kernel window was loaded, before the 16 KB
            ;; OS payload overwrote the ROM's shared sysvars at 0xde00..0xdeff.
            ;; the kernel then mirrors the low page into both banks, brings the
            ;; scheduler online and starts the OS payload thread at 0xc000.
            jp      UKERNEL_LOAD_BASE

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
            ;; the boot signature, then streams the 8 KB reserved area into
            ;; OS_LOAD_BASE. handles 256- and 512-byte sectors: the floppy size
            ;; comes from fd_n$ (set by fd_init from nvram), the hd is always
            ;; 256. lba 0..N fit cylinder 0 in both formats, so no seek is used.
            ;; out: carry set on read error or no signature.
            ;; ----------------------------------------------------------------
            ;; fd_n$ doubles as the boot sector size >> 8 (1 = 256 B, 2 = 512):
            ;; fd_init sets it from the nvram floppy type; the hd path forces 1.
boot_device$:
            ld      (boot_dev$),a

            ;; read the boot record (sector 0) into the transient buffer (low
            ;; ram, vacated by the bootstrap) and verify the signature before
            ;; loading anything. a 512-byte record stays below the rom code.
            ld      hl,#BOOT_RECORD_BUF
            xor     a
            call    boot_read$
            jr      nz,bd_fail$

            ;; signature (0x55 0xaa) sits at buf + sector_size - 2, i.e.
            ;; ((BOOT_RECORD_BUF>>8) - 1 + ssh):0xfe.
            ld      a,(fd_n$)
            add     a,#((BOOT_RECORD_BUF >> 8) - 1)
            ld      h,a
            ld      l,#0xfe
            ld      a,(hl)              ; 0x55 of the 0x55aa signature; the os
            cp      #0x55               ; re-reads + checksums the record later,
            jr      nz,bd_fail$         ; so this is only a quick first-gate

            ;; reserved layout after the boot record (BPB.reserved_sectors must
            ;; match, verified at image build time):
            ;;   lba 1..8   -> 0x0000  micro-kernel (2 KB, mirrored later)
            ;;   lba 9..72  -> 0xc000  shared services + os data (16 KB)
            ;; one loop streams both, hopping the destination up to 0xc000 once
            ;; the 2 KB micro-kernel window is filled. the model hint must be
            ;; written into page 0 before the high OS load begins because the
            ;; ROM's shared sysvars live inside 0xc000..0xffff and are therefore
            ;; overwritten by os.sys.
            ld      hl,#UKERNEL_LOAD_BASE
            ld      c,#1                ; lba
            ld      b,#(UKERNEL_SECTORS + SERVICES_SECTORS)
bd_loop$:
            push    bc                  ; boot_read$ clobbers bc/de
            push    hl
            ld      a,c
            call    boot_read$
            pop     hl
            pop     bc
            jr      nz,bd_fail$
            ld      de,#0x0100
            add     hl,de
            inc     c
            ld      a,c
            cp      #(1 + UKERNEL_SECTORS)  ; finished the micro-kernel window?
            jr      nz,bd_skip$
            call    boot_cache_model$
            ld      hl,#SERVICES_LOAD_BASE  ; hop to the shared services region
bd_skip$:
            djnz    bd_loop$
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

            ;; cache the model hint in the kernel's page-0 dead byte while the
            ;; ROM's own shared sysvars are still intact.
boot_cache_model$:
            ld      a,(model)
            or      #0x80
            ld      (#KERNEL_BOOT_MODEL_ADDR),a
            ret

boot_banner$:
            .db     'P','A','R','T','O','S',0
msg_booting$:
            .db     'B','O','O','T','.','.','.',0
msg_nodev$:
            .db     'N','O',' ','B','O','O','T',0

            ;; the high os load overwrites the rom's _SYSVARS window at
            ;; 0xc000..0xffff, so the live boot-device selector must sit in the
            ;; decompressed stage-1 image instead.
boot_dev$:
            .db     0x00

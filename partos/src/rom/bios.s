            ;; bios.s
            ;;
            ;; early rom-side BIOS setup screen built on the generic menu
            ;; engine. titles are highlighted, the selected row is shown
            ;; inverse, left/right cycle the value, and Ctrl+S / Ctrl+C save or
            ;; discard the configuration back to the rtc-backed nvram block.
            ;;
            ;; 2026-06-14   tstih
            .module bios

            .include "print.inc"

            ;; vertically centred layout: the whole screen sits lower so the
            ;; banner, menu and footer share the visible 26-row text field.
            ;; model name centred on the 80-column screen ((80-len)/2):
            .equ    BIOS_MODEL_WF_X,        29 ; "ISKRA DELTA PARTNER WF" (22)
            .equ    BIOS_MODEL_GDP_X,       34 ; "PARTNER GDP" (11)
            .equ    BIOS_MODEL_Y,           0
            .equ    BIOS_TITLE_X,           35 ; centred "BIOS SETUP" (10)
            .equ    BIOS_TITLE_CRT_Y,       4
            .equ    BIOS_TITLE_GDP_Y,       4
            .equ    BIOS_TITLE_GDP_MODE,    0x00
            .equ    BIOS_FOOTER_X,          15
            .equ    BIOS_FOOTER_Y,          23

            .equ    MENU_KIND_TITLE,        0
            .equ    MENU_KIND_CHOICE,       1
            .equ    MENU_ENTRY_SIZE,        11
            .equ    BIOS_MENU_ENTRY_COUNT,  16
            .equ    BIOS_FIELD_COUNT,       12
            .equ    MENU_ACT_SAVE,          5

            .globl  avdc_set_mode
            .globl  bios_main
            .globl  menu_run
            .globl  model
            .globl  boot_main
            .globl  print_at
            .globl  start_main
            .globl  bios_nvram_cache
            .globl  bios_nvram_write
            .globl  bios_nvram_sum

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; bios_main()
            ;; ----------------------------------------------------------------
bios_main::
            ld      a,(model)
            or      a
            jr      z,bios_plain$

            call    avdc_set_mode       ; arg ignored (80-col only); resets AVDC
            ld      hl,#bios_model_gdp$
            ld      b,#BIOS_MODEL_GDP_X
            jr      bios_print_model$

bios_plain$:
            ld      hl,#bios_clear$
            ld      bc,#0x0000
            xor     a
            call    print_at
            ld      hl,#bios_model_wf$
            ld      b,#BIOS_MODEL_WF_X

bios_print_model$:
            ld      c,#BIOS_MODEL_Y
            ld      a,#PRINT_ATTR_HIGHLIGHT
            call    print_at

            ld      hl,#bios_title$
            ld      b,#BIOS_TITLE_X
            ld      c,#BIOS_TITLE_CRT_Y
            ld      a,#PRINT_ATTR_HIGHLIGHT
            call    print_at

            ;; show the currently saved configuration (if the nvram block is
            ;; valid) instead of the bare factory defaults.
            call    bios_load_settings$

            ;; key hint footer along the bottom of the screen.
            ld      hl,#bios_footer$
            ld      bc,#0x0f19
            xor     a
            call    print_at

            ld      hl,#bios_menu_entries$
            ld      bc,#0x1000
            call    menu_run

            ;; a = MENU_ACT_SAVE (Ctrl+S) or MENU_ACT_EXIT (Ctrl+C)
            cp      #MENU_ACT_SAVE
            call    z,bios_save_settings

            ;; continue straight to the boot path. returning through start_main
            ;; would reopen the timed setup window and make Ctrl+C look stuck.
            ld      a,(model)
            or      a
            call    nz,avdc_set_mode    ; clear GDP before the boot banner
            jp      boot_main

            ;; ----------------------------------------------------------------
            ;; bios_load_settings()
            ;; ----------------------------------------------------------------
            ;; unpack the 2-bit setup fields from the cached nvram block into
            ;; the menu state bytes. only runs when the block checksum is valid;
            ;; otherwise the factory defaults already in the state bytes stand.
            ;;
            ;; nvram layout (matches the device-side readers):
            ;;   byte 1: fd0[7:6] fd1[5:4]
            ;;   byte 2: sda[7:6] sdb[5:4]
            ;;   byte 3: ttys0[7:6] ttys1[5:4] ttys2[3:2] ttys3[1:0]
            ;;   byte 4: lp0[7:6]                 (bios-only field)
            ;;   byte 7: low nibble = checksum adjuster
            ;; ----------------------------------------------------------------
            ;; ----------------------------------------------------------------
            ;; field_setup(<c> control) -> <b> shift, <hl> cache+offset
            ;; ----------------------------------------------------------------
            ;; control byte = (nvram byte offset << 4) | bit shift. shared by
            ;; the load and save loops. clobbers a; preserves c, de.
            ;; ----------------------------------------------------------------
field_setup$:
            ld      a,c
            and     #0x0f
            ld      b,a                    ; b = shift
            ld      a,c
            rrca
            rrca
            rrca
            rrca
            and     #0x0f                  ; a = nvram byte offset
            add     a,#<bios_nvram_cache
            ld      l,a
            ld      a,#>bios_nvram_cache
            adc     a,#0
            ld      h,a                    ; hl = cache + offset
            ret

bios_load_settings$:
            ;; the setup block is always valid here: start_main writes factory
            ;; defaults whenever the checksum is bad, so no guard is needed.
            ld      b,#BIOS_FIELD_COUNT
            ld      de,#bios_states$       ; de -> consecutive state bytes
            ld      hl,#bios_field_ctl$    ; hl -> control bytes (offset<<4|shift)
bls_lp$:
            ld      c,(hl)
            inc     hl
            push    hl
            push    bc                     ; save loop count(b) + control(c)
            call    field_setup$           ; -> b=shift, hl=cache+offset
            ld      a,(hl)
            inc     b
bls_sh$:
            dec     b
            jr      z,bls_st$
            srl     a
            jr      bls_sh$
bls_st$:
            and     #3
            ld      (de),a
            inc     de
            pop     bc
            pop     hl
            djnz    bls_lp$
            ret

            ;; ----------------------------------------------------------------
            ;; bios_save_settings()
            ;; ----------------------------------------------------------------
            ;; pack the menu state bytes back into the 8-byte nvram block, fix
            ;; the checksum so every nibble sums to 0 mod 16, and write it out.
            ;; ----------------------------------------------------------------
bios_save_settings::
            ld      hl,#bios_nvram_cache
            ld      b,#8
            xor     a
bss_clr$:
            ld      (hl),a
            inc     hl
            djnz    bss_clr$

            ;; pack each state byte into its 2-bit nvram field.
            ld      b,#BIOS_FIELD_COUNT
            ld      de,#bios_states$
            ld      hl,#bios_field_ctl$
bss_lp$:
            ld      c,(hl)
            inc     hl
            push    hl
            push    bc                     ; save loop count(b) + control(c)
            call    field_setup$           ; -> b=shift, hl=cache+offset
            ld      a,(de)
            inc     de
            and     #3
            inc     b
bss_sh$:
            dec     b
            jr      z,bss_or$
            add     a,a
            jr      bss_sh$
bss_or$:
            or      (hl)
            ld      (hl),a
            pop     bc
            pop     hl
            djnz    bss_lp$

            ;; checksum adjuster in byte 7 (low nibble). byte 7 is still 0 here,
            ;; so the running sum equals every data nibble; set the adjuster to
            ;; its negative so the grand total reduces to 0 mod 16.
            ld      hl,#bios_nvram_cache
            call    bios_nvram_sum      ; a = sum of all nibbles mod 16
            neg
            and     #0x0f
            ld      (bios_nvram_cache + 7),a

            ld      hl,#bios_nvram_cache
            call    bios_nvram_write
            ret

            ;; "ISKRA DELTA PARTNER WF"
bios_model_wf$:
            .db     'I','S','K','R','A',' ','D','E','L','T','A',' ','P','A','R','T','N','E','R',' ','W','F',0

            ;; "PARTNER GDP"
bios_model_gdp$:
            .db     'P','A','R','T','N','E','R',' ','G','D','P',0

bios_title$:
            .db     'B','I','O','S',' ','S','E','T','U','P',0

bios_footer$:
            .ascii  "ARROWS:MOVE/CHANGE  CTRL+S:SAVE & EXIT  CTRL+C:EXIT"
            .db     0

bios_clear$:
            .db     0x1b,'f',0x0c,0

            ;; ----------------------------------------------------------------
            ;; menu entry layout (11 bytes):
            ;;   +0  kind
            ;;   +1  label/title x
            ;;   +2  y
            ;;   +3  value x (choice only)
            ;;   +4  label/title string ptr
            ;;   +6  choice-table ptr
            ;;   +8  number of choices
            ;;   +9  state-byte ptr
            ;; ----------------------------------------------------------------
bios_menu_entries$:
            .db     MENU_KIND_TITLE,  8,  8,  0
            .dw     bios_serial_title$
            .dw     0
            .db     0
            .dw     0

            .db     MENU_KIND_CHOICE, 8, 10, 22
            .dw     bios_ttys0_label$
            .dw     bios_serial_choice_ptrs$
            .db     4
            .dw     bios_ttys0_state$

            .db     MENU_KIND_CHOICE, 8, 11, 22
            .dw     bios_ttys1_label$
            .dw     bios_serial_choice_ptrs$
            .db     4
            .dw     bios_ttys1_state$

            .db     MENU_KIND_CHOICE, 8, 12, 22
            .dw     bios_ttys2_label$
            .dw     bios_serial_choice_ptrs$
            .db     4
            .dw     bios_ttys2_state$

            .db     MENU_KIND_CHOICE, 8, 13, 22
            .dw     bios_ttys3_label$
            .dw     bios_serial_choice_ptrs$
            .db     4
            .dw     bios_ttys3_state$

            .db     MENU_KIND_TITLE,  8, 16,  0
            .dw     bios_parallel_title$
            .dw     0
            .db     0
            .dw     0

            .db     MENU_KIND_CHOICE, 8, 18, 22
            .dw     bios_lp0_label$
            .dw     bios_parallel_choice_ptrs$
            .db     3
            .dw     bios_lp0_state$

            .db     MENU_KIND_CHOICE, 8, 19, 22
            .dw     bios_lp1_label$
            .dw     bios_parallel_choice_ptrs$
            .db     3
            .dw     bios_lp1_state$

            .db     MENU_KIND_TITLE, 48,  8,  0
            .dw     bios_hd_title$
            .dw     0
            .db     0
            .dw     0

            .db     MENU_KIND_CHOICE, 48, 10, 60
            .dw     bios_sda_label$
            .dw     bios_hd_choice_ptrs$
            .db     4
            .dw     bios_sda_state$

            .db     MENU_KIND_CHOICE, 48, 11, 60
            .dw     bios_sdb_label$
            .dw     bios_hd_choice_ptrs$
            .db     4
            .dw     bios_sdb_state$

            .db     MENU_KIND_TITLE,  48, 14,  0
            .dw     bios_fd_title$
            .dw     0
            .db     0
            .dw     0

            .db     MENU_KIND_CHOICE, 48, 16, 60
            .dw     bios_fd0_label$
            .dw     bios_fd_choice_ptrs$
            .db     4
            .dw     bios_fd0_state$

            .db     MENU_KIND_CHOICE, 48, 17, 60
            .dw     bios_fd1_label$
            .dw     bios_fd_choice_ptrs$
            .db     4
            .dw     bios_fd1_state$

            .db     MENU_KIND_CHOICE, 48, 18, 60
            .dw     bios_fd2_label$
            .dw     bios_fd_choice_ptrs$
            .db     4
            .dw     bios_fd2_state$

            .db     MENU_KIND_CHOICE, 48, 19, 60
            .dw     bios_fd3_label$
            .dw     bios_fd_choice_ptrs$
            .db     4
            .dw     bios_fd3_state$

bios_serial_title$:
            .db     'S','E','R','I','A','L',0
bios_parallel_title$:
            .db     'P','A','R','A','L','L','E','L',0
bios_hd_title$:
            .db     'H','A','R','D',' ','D','I','S','K',0
bios_fd_title$:
            .db     'F','L','O','P','P','Y',0

bios_ttys0_label$:
            .db     'T','T','Y','S','0',0
bios_ttys1_label$:
            .db     'T','T','Y','S','1',0
bios_ttys2_label$:
            .db     'T','T','Y','S','2',0
bios_ttys3_label$:
            .db     'T','T','Y','S','3',0

bios_lp0_label$:
            .db     'L','P','0',0
bios_lp1_label$:
            .db     'L','P','1',0

bios_sda_label$:
            .db     'S','D','A',0
bios_sdb_label$:
            .db     'S','D','B',0

bios_fd0_label$:
            .db     'F','D','0',0
bios_fd1_label$:
            .db     'F','D','1',0
bios_fd2_label$:
            .db     'F','D','2',0
bios_fd3_label$:
            .db     'F','D','3',0

bios_serial_choice_ptrs$:
            .dw     bios_serial_keyboard$
            .dw     bios_serial_terminal$
            .dw     bios_serial_mouse$
            .dw     bios_choice_free$

bios_parallel_choice_ptrs$:
            .dw     bios_choice_free$
            .dw     bios_parallel_printer$
            .dw     bios_parallel_covox$

bios_hd_choice_ptrs$:
            .dw     bios_choice_free$
            .dw     bios_hd_st506$
            .dw     bios_hd_st412$
            .dw     bios_hd_st225$

bios_fd_choice_ptrs$:
            .dw     bios_choice_free$
            .dw     bios_fd_partner$
            .dw     bios_fd_720$
            .dw     bios_fd_360$

bios_serial_keyboard$:
            .db     'K','E','Y','B','O','A','R','D',0
bios_serial_terminal$:
            .db     'T','E','R','M','I','N','A','L',0
bios_serial_mouse$:
            .db     'M','O','U','S','E',0
bios_choice_free$:
            .db     'F','R','E','E',0

bios_parallel_printer$:
            .db     'P','R','I','N','T','E','R',0
bios_parallel_covox$:
            .db     'C','O','V','O','X',0

bios_hd_st506$:
            .db     'S','T','-','5','0','6',0
bios_hd_st412$:
            .db     'S','T','-','4','1','2',0
bios_hd_st225$:
            .db     'S','T','-','2','2','5',0

bios_fd_partner$:
            .db     'P','A','R','T','N','E','R',0
bios_fd_720$:
            .db     'D','O','S','-','7','2','0','K',0
bios_fd_360$:
            .db     'D','O','S','-','3','6','0','K',0

            ;; per-field nvram packing: one control byte per state, in the same
            ;; order as the consecutive state bytes below. high nibble = nvram
            ;; byte offset, low nibble = bit shift of the 2-bit field.
            ;;   ttys0 ttys1 ttys2 ttys3  lp0  lp1   sda  sdb   fd0  fd1  fd2  fd3
bios_field_ctl$:
            .db     0x36, 0x34, 0x32, 0x30, 0x46, 0x44, 0x26, 0x24, 0x16, 0x14, 0x12, 0x10

            ;; live menu selections, in the order bios_field_ctl$ expects. these
            ;; are written as the user cycles choices, but also need their
            ;; factory defaults at boot, so they live in _BOOT (decompressed
            ;; into RAM) rather than _SYSVARS (uninitialized); stage-1 _BOOT
            ;; executes from writable RAM.
bios_states$:
bios_ttys0_state$:
            .db     0
bios_ttys1_state$:
            .db     1
bios_ttys2_state$:
            .db     2
bios_ttys3_state$:
            .db     3
bios_lp0_state$:
            .db     1
bios_lp1_state$:
            .db     0
bios_sda_state$:
            .db     2
bios_sdb_state$:
            .db     0
bios_fd0_state$:
            .db     1
bios_fd1_state$:
            .db     0
bios_fd2_state$:
            .db     0
bios_fd3_state$:
            .db     0

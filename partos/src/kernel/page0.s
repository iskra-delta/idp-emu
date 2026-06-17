            ;; page0.s
            ;;
            ;; yos kernel page 0 template. this low page is meant to be
            ;; installed into both bank 0 and bank 1 after the bootstrap has
            ;; loaded and entered yos in shared memory. the layout follows the
            ;; yos crt0rom style: fixed 8-byte rst slots, inline return paths,
            ;; and the remaining low-page info block before nmi at 0x66.
            ;;
            ;; 2026-06-14   tstih
            .module page0

            .globl  __sys_page0
            .globl  __sys_page0_end
            .globl  __sys_page0_install
            .globl  __sys_page0_free
            .globl  __sys_entry
            .globl  __sys_vec_rst08
            .globl  __sys_vec_rst10
            .globl  __sys_vec_rst18
            .globl  __sys_vec_rst20
            .globl  __sys_vec_rst28
            .globl  __sys_vec_rst30
            .globl  __sys_vec_rst38
            .globl  __sys_vec_nmi
            .globl  __sys_version
            .globl  __sys_model
            .globl  __sys_flags
            .globl  __sys_meta1
            .globl  __sys_info
            .globl  __sys_info_end
            .globl  __sys_nvram_cache
            .globl  __sys_info_reserved

            .equ    BANK0_PORT,        0x88 ; hardware bank 1, logical bank 0
            .equ    BANK1_PORT,        0x90 ; hardware bank 2, logical bank 1
            .equ    NVRAM_PORT_BASE,   0xa8
            .equ    NVRAM_SIZE,        8
            .equ    PAGE0_INSTALL_OFF, 0x006b
            .equ    PAGE0_SIZE,        0x0100

            ;; ----------------------------------------------------------------
            ;; page-0 template image
            ;; ----------------------------------------------------------------
            ;; this area is copied by kernel bootstrap into bank 0 and bank 1
            ;; page 0. it is not executed in place, so it stays relocatable.
            ;; rst 0 keeps the compact jp-to-entry form because bytes 0x04..0x07
            ;; are reserved for version/model/flags/meta. the remaining vectors
            ;; load their real targets from the shared-memory vector table.
            ;; ----------------------------------------------------------------
            .area   _PAGE0
__sys_page0::

            ;; ----------------------------------------------------------------
            ;; rst 0x00
            ;; ----------------------------------------------------------------
            ;; bytes 0x0002-0x0003 are the operand of this jp instruction, so
            ;; they already carry the current yos entry address.
            ;; ----------------------------------------------------------------
            di
            jp      __sys_entry

__sys_version::
            .db     0                   ; high nibble = major, low nibble = minor
__sys_model::
            .db     0                   ; bit 0 = has graphics (gdp)
                                        ; bits 1-3 = floppy count
                                        ; bits 4-5 = hard-drive count
                                        ; bits 6-7 = reserved
__sys_flags::
            .db     0                   ; bit 0 = current bank (0/1)
__sys_meta1::
            .db     0

            ;; rst 0x08
            ld      hl,(#__sys_vec_rst08)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x10
            ld      hl,(#__sys_vec_rst10)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x18
            ld      hl,(#__sys_vec_rst18)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x20
            ld      hl,(#__sys_vec_rst20)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x28
            ld      hl,(#__sys_vec_rst28)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x30
            ld      hl,(#__sys_vec_rst30)
            jp      (hl)
            .db     0,0,0,0

            ;; rst 0x38 - im 1 interrupt entry
            ld      hl,(#__sys_vec_rst38)
            jp      (hl)
            .db     0,0,0,0

            ;; ----------------------------------------------------------------
            ;; 38-byte low-page info block.
            ;; ----------------------------------------------------------------
            ;; keep bank-local cache here. callers that need identical content
            ;; across both banks must mirror writes into both page-0 copies.
            ;; ----------------------------------------------------------------
__sys_info::
__sys_nvram_cache::
            .db     0,0,0,0,0,0,0,0
__sys_info_reserved::
            .db     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
            .db     0,0,0,0,0,0,0,0,0,0,0,0,0,0
__sys_info_end::

            ;; nmi interrupt 0x66
            ld      hl,(#__sys_vec_nmi)
            jp      (hl)
            .db     0
__sys_page0_end::

            ;; the copied low-page template ends before 0x006b. keep the real
            ;; installer body inside the reserved 256-byte page block so it
            ;; does not also consume shared _CODE space.
            .ds     PAGE0_INSTALL_OFF - (__sys_page0_end-__sys_page0)
__sys_page0_install::
            ld      (page0_ret$),hl
            ld      (#__sys_version),a
            ld      a,b
            ld      (#__sys_model),a
            ld      a,c
            and     #0xfe              ; per-bank bit is supplied during copy
            ld      (#__sys_flags),a
            ld      a,d
            ld      (#__sys_meta1),a
            ld      hl,#__sys_nvram_cache
            ld      b,#NVRAM_SIZE
            ld      c,#NVRAM_PORT_BASE
page0_nvram_copy$:
            in      a,(c)
            ld      (hl),a
            inc     hl
            inc     c
            djnz    page0_nvram_copy$

            ;; install page 0 into logical bank 0
            xor     a
            out     (BANK0_PORT),a
            ld      hl,#0x0000
            ld      de,#0x0001
            ld      bc,#0x00ff
            ld      (hl),a
            ldir
            ld      a,(#__sys_flags)
            and     #0xfe
            ld      hl,#__sys_page0
            ld      de,#0x0000
            ld      bc,#(__sys_page0_end-__sys_page0)
            ldir
            ld      (#(__sys_flags-__sys_page0)),a

            ;; install page 0 into logical bank 1
            xor     a
            out     (BANK1_PORT),a
            ld      hl,#0x0000
            ld      de,#0x0001
            ld      bc,#0x00ff
            ld      (hl),a
            ldir
            ld      a,(#__sys_flags)
            or      #0x01
            ld      hl,#__sys_page0
            ld      de,#0x0000
            ld      bc,#(__sys_page0_end-__sys_page0)
            ldir
            ld      (#(__sys_flags-__sys_page0)),a

            ;; return to logical bank 0 and continue without stack use
            xor     a
            out     (BANK0_PORT),a
            ld      hl,(page0_ret$)
            jp      (hl)

page0_ret$:
            .db     0,0
__sys_page0_free::
            .ds     PAGE0_SIZE - (__sys_page0_free-__sys_page0)

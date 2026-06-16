            ;; main.s
            ;;
            ;; tiny ROM harness for BIOS print_at testing
            ;;
            ;; 2026-06-13   tstih
            .module print_harness

            .include "../../partos/src/bios/core/dev.inc"
            .include "../../partos/src/bios/drivers/gdp.inc"
            .include "../../partos/src/bios/util/print.inc"

            .globl  dev_init
            .globl  dev_list
            .globl  gdp_open
            .globl  print_at

            .area   _CODE

start::
            call    dev_init

            in      a,(GDP_PORT_PIO_COMMON)
            cp      #0xff
            jr      z,print_start$

            in      a,(GDP_PORT_AVDC_STATUS)
            cp      #0xff
            jr      z,print_start$

            ld      hl,#test_gdp_dev_template$
            ld      de,#test_gdp_dev$
            ld      bc,#DEV_SIZE
            ldir

            ld      hl,#test_gdp_dev$
            ld      (dev_list),hl

            ld      hl,#test_gdp_dev$
            call    gdp_open
            ld      a,h
            or      l
            jr      nz,fail$

print_start$:
            ld      a,#PRINT_ATTR_HIGHLIGHT
            ld      b,#1
            ld      c,#0
            ld      hl,#msg_hi$
            call    print_at
            ld      a,h
            or      l
            jr      nz,fail$

            ld      a,#PRINT_ATTR_INVERSE
            ld      b,#3
            ld      c,#2
            ld      hl,#msg_inv$
            call    print_at
            ld      a,h
            or      l
            jr      nz,fail$

            ld      a,#PRINT_ATTR_NORMAL
            ld      b,#0
            ld      c,#4
            ld      hl,#msg_norm$
            call    print_at
            ld      a,h
            or      l
            jr      nz,fail$

halt$:
            halt
            jr      halt$

fail$:
            jr      fail$
msg_hi$:
            .db     'H',0
msg_inv$:
            .db     'H','E','L','L','O',0
msg_norm$:
            .db     'N',0

test_gdp_dev_template$:
            .dw     0x0000
            .db     'g','d','p',0,0,0,0,0
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     GDP_F_AVDC
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .db     0x00
            .dw     0x0000

            .area   _SYSVARS
test_gdp_dev$:
            .ds     DEV_SIZE

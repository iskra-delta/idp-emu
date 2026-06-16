            ;; gdp.s
            ;;
            ;; partner gdp board bios driver
            ;;
            ;; minimal text-mode support:
            ;;   - probe publishes one "gdp" device when the board responds
            ;;   - write pushes text through the AVDC at the current cursor
            ;;   - ioctl handles cursor position, current attribute and
            ;;     cursor on/off
            ;;
            ;; 2026-06-13   tstih
            .module gdp

            .include "dev.inc"
            .include "drv.inc"
            .include "gdp.inc"

            .globl  delay_1ms
            .globl  drv_reset_dev
            .globl  drv_close_nop
            .globl  drv_read_unsupported
            .globl  gdp_probe

            .globl  gdp_write
            .globl  gdp_ioctl

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *chain <= gdp_probe()
            ;; ----------------------------------------------------------------
            ;; probes the optional gdp video board by touching only board-local
            ;; ports. on non-gdp machines those ports float to 0xff. on success
            ;; a single gdp device is published; its private data stores the
            ;; key board ports and feature bits so later text/ioctl code can
            ;; choose ef9367, avdc or both.
            ;;
            ;; output(s):
            ;;  hl  ... gdp device chain head, 0x0000 if no gdp board present
            ;; destroys:
            ;;  a, bc, de
            ;; ----------------------------------------------------------------
gdp_probe::
            in      a,(GDP_PORT_PIO_COMMON)
            cp      #0xff
            jr      z,gdp_none$

            ld      c,#GDP_F_PIO

            in      a,(GDP_PORT_EF_STATUS)
            cp      #0xff
            jr      z,gdp_avdc$
            ld      a,c
            or      #GDP_F_EF9367
            ld      c,a

gdp_avdc$:
            in      a,(GDP_PORT_AVDC_STATUS)
            cp      #0xff
            jr      z,gdp_make$
            ld      a,c
            or      #GDP_F_AVDC
            ld      c,a

gdp_make$:
            ld      hl,#gdp_dev0$
            ld      de,#gdp_dev_drv
            call    drv_reset_dev

            push    hl
            ld      de,#DEV_DATA
            add     hl,de
            ld      a,#GDP_PORT_PIO_COMMON
            ld      (hl),a
            inc     hl
            ld      a,#GDP_PORT_EF_CMD
            ld      (hl),a
            inc     hl
            ld      a,#GDP_PORT_EF_STATUS
            ld      (hl),a
            inc     hl
            ld      a,#GDP_PORT_AVDC_DATA
            ld      (hl),a
            inc     hl
            ld      a,#GDP_PORT_AVDC_STATUS
            ld      (hl),a
            inc     hl
            ld      (hl),c
            pop     hl
            ret

gdp_none$:
            ld      hl,#0x0000
            ret

gdp_open::
            push    hl
            ld      bc,#DEV_FLAGS
            add     hl,bc
            bit     0,(hl)
            jr      z,gdpo_check$
            pop     hl
            ld      hl,#DRV_OK
            ret
gdpo_check$:
            pop     hl
            call    gdp_has_avdc$
            jr      nz,gdpo_init$
            ld      hl,#DRV_ERR
            ret

gdpo_init$:
            ld      (gdp_tmp_dev$),hl
            ld      a,#0x07
            out     (GDP_PORT_PIO_CTRL_A),a
            out     (GDP_PORT_PIO_CTRL_B),a
            ld      a,#0x0f
            out     (GDP_PORT_PIO_CTRL_A),a
            out     (GDP_PORT_PIO_CTRL_B),a
            ld      a,#0x18
            out     (GDP_PORT_PIO_COMMON),a
            ld      a,#0x6d
            out     (GDP_PORT_PIO_TEXT),a

            xor     a
            out     (GDP_PORT_AVDC_STATUS),a
            out     (GDP_PORT_AVDC_COMMON),a

            ld      hl,#0x0000
            call    delay_1ms
            ld      hl,#0x0000
            call    delay_1ms
            ld      hl,#0x0000
            call    delay_1ms

            xor     a
            out     (GDP_PORT_AVDC_CUR_LO),a
            out     (GDP_PORT_AVDC_CUR_HI),a
            out     (GDP_PORT_AVDC_STATUS + 5),a
            out     (GDP_PORT_AVDC_STATUS + 6),a
            out     (GDP_PORT_AVDC_STATUS + 1),a
            out     (GDP_PORT_AVDC_STATUS + 2),a

            ld      a,#0x10
            out     (GDP_PORT_AVDC_STATUS),a
            ld      hl,#gdp_avdc_init$
            ld      c,#GDP_PORT_AVDC_INIT
            ld      b,#10
            otir

            ld      a,#0x3d
            out     (GDP_PORT_AVDC_STATUS),a
            xor     a
            out     (GDP_PORT_AVDC_CUR_LO),a
            out     (GDP_PORT_AVDC_CUR_HI),a

            ld      a,#GDP_AVDC_CMD_SET_PTR
            out     (GDP_PORT_AVDC_STATUS),a
            ld      a,#<GDP_CLEAR_END
            out     (GDP_PORT_AVDC_INIT),a
            ld      a,#>GDP_CLEAR_END
            out     (GDP_PORT_AVDC_INIT),a

            ld      a,#0x20
            out     (GDP_PORT_AVDC_DATA),a
            xor     a
            out     (GDP_PORT_AVDC_ATTR),a
            ld      a,#GDP_AVDC_CMD_FILL
            out     (GDP_PORT_AVDC_STATUS),a

            ld      hl,#0x0000
            call    gdp_set_cursor$
            ld      de,#GDP_TEXT_BASE
            ld      b,#GDP_ROWS
            ld      l,#GDP_ATTR_NORMAL
gdpo_rowtab$:
            ld      a,e
            call    gdp_putchar$
            ld      a,d
            call    gdp_putchar$
            ld      a,e
            add     a,#GDP_COLS
            ld      e,a
            jr      nc,gdpo_rowtab_nc$
            inc     d
gdpo_rowtab_nc$:
            djnz    gdpo_rowtab$

            ld      hl,(gdp_tmp_dev$)
            push    hl
            ld      bc,#DEV_FLAGS
            add     hl,bc
            set     0,(hl)
            pop     hl

            ld      hl,(gdp_tmp_dev$)
            xor     a
            call    gdp_store_attr$
            ld      hl,(gdp_tmp_dev$)
            ld      b,#0
            ld      c,#0
            call    gdp_apply_xy$
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; gdp_write(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
            ;; writes text bytes through the AVDC at the current cursor using
            ;; the per-device current attribute. printable bytes advance the
            ;; hardware cursor, while CR/LF reposition it through ioctl-like
            ;; cursor updates.
            ;; ----------------------------------------------------------------
gdp_write::
            ld      a,b
            or      c
            jr      nz,gdpw_check$
            ld      hl,#DRV_OK
            ret
gdpw_check$:
            call    gdp_has_avdc$
            jr      nz,gdpw_go$
            ld      hl,#DRV_ERR
            ret
gdpw_go$:
            ld      (gdp_tmp_dev$),hl
gdpw_loop$:
            ld      a,b
            or      c
            jr      z,gdpw_done$
            ld      a,(de)
            inc     de
            cp      #0x0d
            jr      z,gdpw_cr$
            cp      #0x0a
            jr      z,gdpw_lf$

            ld      (gdp_tmp_ch$),a
            push    de
            ld      hl,(gdp_tmp_dev$)
            ld      de,#DEV_DATA + GDP_DATA_TEXT_ATTR
            add     hl,de
            ld      l,(hl)
            ld      a,(gdp_tmp_ch$)
            call    gdp_putchar$
            pop     de

            push    de
            ld      hl,(gdp_tmp_dev$)
            ld      de,#DEV_DATA + GDP_DATA_CUR_X
            add     hl,de
            ld      a,(hl)
            inc     a
            cp      #GDP_COLS
            jr      c,gdpw_store_x$

            push    bc
            xor     a
            ld      b,a
            inc     hl
            ld      a,(hl)
            cp      #GDP_ROWS - 1
            jr      nc,gdpw_wrap_set$
            inc     a
gdpw_wrap_set$:
            ld      c,a
            ld      hl,(gdp_tmp_dev$)
            call    gdp_apply_xy$
            pop     bc
            pop     de
            jr      gdpw_next$

gdpw_store_x$:
            ld      (hl),a
            pop     de
            jr      gdpw_next$

gdpw_cr$:
            push    de
            push    bc
            ld      hl,(gdp_tmp_dev$)
            ld      de,#DEV_DATA + GDP_DATA_CUR_Y
            add     hl,de
            ld      c,(hl)
            ld      b,#0
            ld      hl,(gdp_tmp_dev$)
            call    gdp_apply_xy$
            pop     bc
            pop     de
            jr      gdpw_next$

gdpw_lf$:
            push    de
            push    bc
            ld      hl,(gdp_tmp_dev$)
            ld      de,#DEV_DATA + GDP_DATA_CUR_Y
            add     hl,de
            ld      a,(hl)
            cp      #GDP_ROWS - 1
            jr      nc,gdpw_lf_keep$
            inc     a
gdpw_lf_keep$:
            ld      c,a
            ld      b,#0
            ld      hl,(gdp_tmp_dev$)
            call    gdp_apply_xy$
            pop     bc
            pop     de

gdpw_next$:
            dec     bc
            jr      gdpw_loop$

gdpw_done$:
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; gdp_ioctl(<hl> *dev, <de> *params, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; supported commands:
            ;;   GDP_IOCTL_GETPOS / SETPOS   params -> { x, y }
            ;;   GDP_IOCTL_GETATTR / SETATTR params -> uint8_t
            ;;   GDP_IOCTL_CURSOR_OFF / ON   params ignored
            ;; ----------------------------------------------------------------
gdp_ioctl::
            ld      a,b
            or      a
            jr      z,gdpi_cmd$
            ld      hl,#DRV_ERR
            ret
gdpi_cmd$:
            ld      a,c
            cp      #GDP_IOCTL_GETPOS
            jr      z,gdpi_getpos$
            cp      #GDP_IOCTL_SETPOS
            jr      z,gdpi_setpos$
            cp      #GDP_IOCTL_GETATTR
            jr      z,gdpi_getattr$
            cp      #GDP_IOCTL_SETATTR
            jr      z,gdpi_setattr$
            cp      #GDP_IOCTL_CURSOR_OFF
            jr      z,gdpi_cursor_off$
            cp      #GDP_IOCTL_CURSOR_ON
            jr      z,gdpi_cursor_on$
            ld      hl,#DRV_ERR
            ret

gdpi_getpos$:
            push    hl
            ld      bc,#DEV_DATA + GDP_DATA_CUR_X
            add     hl,bc
            ld      a,(hl)
            ld      (de),a
            inc     de
            inc     hl
            ld      a,(hl)
            ld      (de),a
            pop     hl
            ld      hl,#DRV_OK
            ret

gdpi_setpos$:
            call    gdp_has_avdc$
            jr      nz,gdpi_setpos_go$
            ld      hl,#DRV_ERR
            ret
gdpi_setpos_go$:
            ld      a,(de)
            cp      #GDP_COLS
            jr      nc,gdpi_bad$
            ld      b,a
            inc     de
            ld      a,(de)
            cp      #GDP_ROWS
            jr      nc,gdpi_bad$
            ld      c,a
            call    gdp_apply_xy$
            ld      hl,#DRV_OK
            ret

gdpi_getattr$:
            push    hl
            ld      bc,#DEV_DATA + GDP_DATA_TEXT_ATTR
            add     hl,bc
            ld      a,(hl)
            ld      (de),a
            pop     hl
            ld      hl,#DRV_OK
            ret

gdpi_setattr$:
            ld      a,(de)
            call    gdp_store_attr$
            ld      hl,#DRV_OK
            ret

gdpi_cursor_off$:
            call    gdp_has_avdc$
            jr      nz,gdpi_co_go$
            ld      hl,#DRV_ERR
            ret
gdpi_co_go$:
            call    gdp_cursor_off$
            ld      hl,#DRV_OK
            ret

gdpi_cursor_on$:
            call    gdp_has_avdc$
            jr      nz,gdpi_cn_go$
            ld      hl,#DRV_ERR
            ret
gdpi_cn_go$:
            call    gdp_cursor_on$
            ld      hl,#DRV_OK
            ret

gdpi_bad$:
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; raw AVDC helpers
            ;; ----------------------------------------------------------------
gdp_has_avdc$:
            push    hl
            push    bc
            ld      bc,#DEV_DATA + GDP_DATA_FLAGS
            add     hl,bc
            ld      a,(hl)
            and     #GDP_F_AVDC
            pop     bc
            pop     hl
            ret

gdp_store_attr$:
            and     #GDP_ATTR_MASK
            push    af
            push    bc
            ld      bc,#DEV_DATA + GDP_DATA_TEXT_ATTR
            add     hl,bc
            pop     bc
            pop     af
            ld      (hl),a
            ret

gdp_apply_xy$:
            push    bc
            push    hl
            ld      l,c
            call    gdp_wait_mem_acc$
            call    gdp_rowptr_raw$
            ld      d,#0
            ld      e,b
            add     hl,de
            call    gdp_set_cursor$
            pop     hl
            pop     bc
            push    hl
            ld      de,#DEV_DATA + GDP_DATA_CUR_X
            add     hl,de
            ld      (hl),b
            inc     hl
            ld      (hl),c
            pop     hl
            ret

gdp_wait_rdy$:
            in      a,(GDP_PORT_AVDC_STATUS)
            and     #GDP_AVDC_STS_RDY
            jr      z,gdp_wait_rdy$
            ret

gdp_wait_mem_acc$:
            in      a,(GDP_PORT_AVDC_COMMON)
            and     #GDP_AVDC_MEM_ACC
            jr      z,gdp_wait_mem_acc$
gdp_wait_mem_acc2$:
            in      a,(GDP_PORT_AVDC_COMMON)
            and     #GDP_AVDC_MEM_ACC
            jr      nz,gdp_wait_mem_acc2$
            ret

gdp_set_pointer$:
            call    gdp_wait_rdy$
            ld      a,#GDP_AVDC_CMD_SET_PTR
            out     (GDP_PORT_AVDC_STATUS),a
            ld      a,l
            out     (GDP_PORT_AVDC_INIT),a
            ld      a,h
            out     (GDP_PORT_AVDC_INIT),a
            ret

gdp_read_at_pointer$:
            push    hl
            call    gdp_set_pointer$
            ld      a,#GDP_AVDC_CMD_RDPTR
            out     (GDP_PORT_AVDC_STATUS),a
            call    gdp_wait_rdy$
            in      a,(GDP_PORT_AVDC_DATA)
            ld      e,a
            in      a,(GDP_PORT_AVDC_ATTR)
            ld      d,a
            pop     hl
            ret

gdp_rowptr_raw$:
            ld      h,#0
            add     hl,hl
            call    gdp_read_at_pointer$
            push    de
            inc     hl
            call    gdp_read_at_pointer$
            ld      h,e
            pop     de
            ld      l,e
            ld      d,h
            ld      e,l
            ret

gdp_set_cursor$:
            call    gdp_wait_mem_acc$
            call    gdp_wait_rdy$
            ld      a,l
            out     (GDP_PORT_AVDC_CUR_LO),a
            ld      a,h
            out     (GDP_PORT_AVDC_CUR_HI),a
            ret

gdp_putchar$:
            ld      (gdp_tmp_ch$),a
            call    gdp_wait_mem_acc$
            call    gdp_wait_rdy$
            ld      a,(gdp_tmp_ch$)
            out     (GDP_PORT_AVDC_DATA),a
            ld      a,l
            out     (GDP_PORT_AVDC_ATTR),a
            call    gdp_wait_rdy$
            call    gdp_wait_rdy$
            ld      a,#GDP_AVDC_CMD_WAC
            out     (GDP_PORT_AVDC_STATUS),a
            ret

gdp_cursor_on$:
            call    gdp_wait_rdy$
            ld      a,#GDP_AVDC_CMD_CURS_ON
            out     (GDP_PORT_AVDC_STATUS),a
            ret

gdp_cursor_off$:
            call    gdp_wait_rdy$
            ld      a,#GDP_AVDC_CMD_CURS_OFF
            out     (GDP_PORT_AVDC_STATUS),a
            ret

gdp_dev_drv::
            .dw     0x0000
            .dw     gdp_probe
            .dw     gdp_open
            .dw     drv_close_nop
            .dw     drv_read_unsupported
            .dw     gdp_write
            .dw     gdp_ioctl

gdp_avdc_init$:
            .db     0xd0,0x3e,0xbf,0x05,0x99,0x83,0x0b,0xea,0x00,0x30

gdp_dev0$:
            .dw     0x0000
            .db     'g','d','p',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     gdp_dev_drv

            .area   _SYSVARS
gdp_tmp_dev$:
            .ds     2
gdp_tmp_ch$:
            .ds     1

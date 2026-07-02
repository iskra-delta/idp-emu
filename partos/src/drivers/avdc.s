            ;; avdc.s
            ;;
            ;; Partner GDP-board AVDC text driver.
            ;;
            ;; This is the kernel-facing text console on graphics-capable
            ;; machines. It uses the SCN2674 row table, so scrolling is done by
            ;; rotating line pointers instead of copying the whole screen.
            ;;
            ;; supported operations:
            ;;   - dev.s publishes one "avdc" device on graphics-capable models
            ;;   - open initializes 132x26 row-table text mode
            ;;   - write handles CR/LF, wraps, and scrolls automatically
            ;;   - read returns visible screen characters from the current cursor
            ;;   - ioctl supports: set attribute, clear, cursor show/hide, gotoxy
            ;;
            ;; compatibility:
            ;;   - legacy gdp_* symbols are kept as zero-cost aliases so older
            ;;     assembly test code still links while the kernel moves to avdc_*
            ;;
            ;; 2026-06-17   tstih
            .module avdc

            .include "dev.inc"
            .include "drv.inc"
            .include "avdc.inc"

            .globl  delay_1ms
            .globl  drv_close_nop
            .globl  drv_signal_done
            .globl  avdc_init
            .globl  gdp_init
            .globl  avdc_dev0
            .globl  avdc_open
            .globl  gdp_open
            .globl  avdc_read
            .globl  gdp_read
            .globl  avdc_write
            .globl  gdp_write
            .globl  avdc_ioctl
            .globl  gdp_ioctl
            .globl  avdc_dev_drv
            .globl  gdp_dev_drv
            .globl  drv_zero_ok_bc_ix
            .globl  fd_dev_drv

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; avdc_open(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; initializes the AVDC into 132-column row-table text mode.
            ;; the full text backing store is cleared once, then the row table
            ;; is built so later scrolling can rotate rows cheaply.
            ;; ----------------------------------------------------------------
avdc_open::
gdp_open::
            push    hl
            ld      bc,#DEV_FLAGS
            add     hl,bc
            bit     0,(hl)
            jr      z,avdco_check$
            pop     hl
            ld      hl,#DRV_OK
            ret
avdco_check$:
            pop     hl

avdco_init$:
            ld      (avdc_tmp_dev$),hl

            ;; program the GDP-board local PIO for text mode routing.
            ld      a,#0x07
            out     (AVDC_PORT_PIO_CTRL_A),a
            out     (AVDC_PORT_PIO_CTRL_B),a
            ld      a,#0x0f
            out     (AVDC_PORT_PIO_CTRL_A),a
            out     (AVDC_PORT_PIO_CTRL_B),a
            ld      a,#0x18
            out     (AVDC_PORT_PIO_COMMON),a
            ld      a,#AVDC_TEXT_CTL_132
            out     (AVDC_PORT_PIO_TEXT),a

            ;; hard reset the AVDC side of the board.
            xor     a
            out     (AVDC_PORT_AVDC_STATUS),a
            out     (AVDC_PORT_AVDC_COMMON),a

            ld      hl,#0x0000
            call    delay_1ms
            ld      hl,#0x0000
            call    delay_1ms
            ld      hl,#0x0000
            call    delay_1ms

            xor     a
            out     (AVDC_PORT_AVDC_CUR_LO),a
            out     (AVDC_PORT_AVDC_CUR_HI),a
            out     (AVDC_PORT_AVDC_STATUS + 5),a
            out     (AVDC_PORT_AVDC_STATUS + 6),a
            out     (AVDC_PORT_AVDC_STATUS + 1),a
            out     (AVDC_PORT_AVDC_STATUS + 2),a

            ;; load the board's 132-column init sequence.
            ld      a,#0x10
            out     (AVDC_PORT_AVDC_STATUS),a
            ld      hl,#avdc_init_block$
            ld      c,#AVDC_PORT_AVDC_INIT
            ld      b,#10
            otir

            ;; enable display + cursor path.
            ld      a,#0x3d
            out     (AVDC_PORT_AVDC_STATUS),a

            ;; clear the full visible/storeable text area, including the future
            ;; row-table region. row-table bytes are rebuilt immediately after.
            xor     a
            out     (AVDC_PORT_AVDC_CUR_LO),a
            out     (AVDC_PORT_AVDC_CUR_HI),a
            ld      hl,#AVDC_CLEAR_END
            call    avdc_set_pointer$
            ld      a,#0x20
            out     (AVDC_PORT_AVDC_DATA),a
            xor     a
            out     (AVDC_PORT_AVDC_ATTR),a
            ld      a,#AVDC_CMD_FILL
            out     (AVDC_PORT_AVDC_STATUS),a

            ;; rebuild the row table at address 0 so every visible row points
            ;; at its backing text line in the linear text buffer.
            ld      hl,#0x0000
            call    avdc_set_cursor$
            ld      de,#AVDC_TEXT_BASE
            ld      b,#AVDC_ROWS
            ld      l,#AVDC_ATTR_NORMAL
avdco_rowtab$:
            ld      a,e
            call    avdc_putchar$
            ld      a,d
            call    avdc_putchar$
            ld      a,e
            add     a,#AVDC_COLS
            ld      e,a
            jr      nc,avdco_rowtab_nc$
            inc     d
avdco_rowtab_nc$:
            djnz    avdco_rowtab$

            ld      hl,(avdc_tmp_dev$)
            push    hl
            ld      bc,#DEV_FLAGS
            add     hl,bc
            set     0,(hl)
            pop     hl

            ld      hl,(avdc_tmp_dev$)
            xor     a
            call    avdc_store_attr$
            ld      hl,(avdc_tmp_dev$)
            ld      b,#0
            ld      c,#0
            call    avdc_apply_xy$
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; avdc_read(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
            ;; reads visible screen characters from the current cursor position.
            ;; the cursor advances across the row table, wrapping from the last
            ;; column to the first column of the next row; after the last row it
            ;; wraps back to row 0. this keeps read side-effect free on screen
            ;; contents while still giving callers sequential screen access.
            ;; ----------------------------------------------------------------
avdc_read::
gdp_read::
            call    drv_zero_ok_bc_ix
            ret     z
            ld      (avdc_tmp_dev$),hl
avdcr_loop$:
            ld      a,b
            or      c
            jr      z,avdcr_done$

            push    de
            push    bc
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            ld      b,(hl)
            inc     hl
            ld      c,(hl)
            ld      l,c
            call    avdc_wait_mem_acc$
            call    avdc_rowptr_raw$
            ld      d,#0
            ld      e,b
            add     hl,de
            call    avdc_read_at_pointer$
            ld      a,e
            ld      (avdc_tmp_ch$),a
            pop     bc
            pop     hl
            ld      a,(avdc_tmp_ch$)
            ld      (hl),a
            inc     hl
            ex      de,hl
            call    avdc_step_read$
            dec     bc
            jr      avdcr_loop$

avdcr_done$:
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            ld      b,(hl)
            inc     hl
            ld      c,(hl)
            ld      hl,(avdc_tmp_dev$)
            call    avdc_apply_xy$
            call    drv_signal_done
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; avdc_write(<hl> *dev, <de> *buf, <bc> count)
            ;; ----------------------------------------------------------------
            ;; writes text through the AVDC at the current cursor using the
            ;; per-device current attribute. CR homes X on the current row, LF
            ;; means "new line" (X=0, Y++, scroll if already on the last row).
            ;; ----------------------------------------------------------------
avdc_write::
gdp_write::
            call    drv_zero_ok_bc_ix
            ret     z
            ld      (avdc_tmp_dev$),hl
            call    avdc_load_attr_a$
            ld      (avdc_tmp_attr$),a
avdcw_loop$:
            ld      a,b
            or      c
            jr      z,avdcw_done$
            ld      a,(de)
            inc     de
            cp      #0x0d
            jr      z,avdcw_cr$
            cp      #0x0a
            jr      z,avdcw_lf$
            cp      #0x08
            jr      z,avdcw_bs$
            cp      #0x7f
            jr      z,avdcw_bs$

            ld      (avdc_tmp_ch$),a
            ld      a,(avdc_tmp_attr$)
            ld      l,a
            ld      a,(avdc_tmp_ch$)
            call    avdc_putchar$
            call    avdc_step_write$
            jr      avdcw_next$

avdcw_cr$:
            call    avdc_carriage_return$
            jr      avdcw_next$

avdcw_lf$:
            call    avdc_newline$
            jr      avdcw_next$

avdcw_bs$:
            call    avdc_backspace$

avdcw_next$:
            dec     bc
            jr      avdcw_loop$

avdcw_done$:
            call    drv_signal_done
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; avdc_ioctl(<hl> *dev, <de> *params, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; supported commands:
            ;;   AVDC_IOCTL_SETATTR  params -> uint8_t
            ;;   AVDC_IOCTL_CLEAR    params ignored
            ;;   AVDC_IOCTL_CURSOR   params -> uint8_t (0 hide, non-zero show)
            ;;   AVDC_IOCTL_GOTOXY   params -> { x, y }
            ;; ----------------------------------------------------------------
avdc_ioctl::
gdp_ioctl::
            ld      a,b
            or      a
            jr      z,avdci_cmd$
            ld      hl,#DRV_ERR
            ret
avdci_cmd$:
            ld      a,c
            cp      #AVDC_IOCTL_SETATTR
            jr      z,avdci_setattr$
            cp      #AVDC_IOCTL_CLEAR
            jr      z,avdci_clear$
            cp      #AVDC_IOCTL_CURSOR
            jr      z,avdci_cursor$
            cp      #AVDC_IOCTL_GOTOXY
            jr      z,avdci_gotoxy$
            ld      hl,#DRV_ERR
            ret

avdci_setattr$:
            ld      a,(de)
            call    avdc_store_attr$
            ld      hl,#DRV_OK
            ret

avdci_clear$:
            ld      (avdc_tmp_dev$),hl
            call    avdc_clear_screen$
            ld      hl,#DRV_OK
            ret

avdci_cursor$:
            ld      a,(de)
            or      a
            jr      z,avdci_cursor_off$
            call    avdc_cursor_on$
            ld      hl,#DRV_OK
            ret
avdci_cursor_off$:
            call    avdc_cursor_off$
            ld      hl,#DRV_OK
            ret

avdci_gotoxy$:
            ld      a,(de)
            cp      #AVDC_COLS
            jr      nc,avdci_bad$
            ld      b,a
            inc     de
            ld      a,(de)
            cp      #AVDC_ROWS
            jr      nc,avdci_bad$
            ld      c,a
            call    avdc_apply_xy$
            ld      hl,#DRV_OK
            ret

avdci_bad$:
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; helper: write-side cursor maintenance
            ;; ----------------------------------------------------------------
avdc_carriage_return$:
            push    de
            push    bc
            push    hl
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_Y
            add     hl,de
            ld      c,(hl)
            ld      b,#0
            ld      hl,(avdc_tmp_dev$)
            call    avdc_apply_xy$
            pop     hl
            pop     bc
            pop     de
            ret

avdc_backspace$:
            push    de
            push    bc
            push    hl
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            ld      a,(hl)
            or      a
            jr      z,avdcb_done$
            dec     (hl)
            ld      b,(hl)
            inc     hl
            ld      c,(hl)
            ld      hl,(avdc_tmp_dev$)
            call    avdc_apply_xy$
avdcb_done$:
            pop     hl
            pop     bc
            pop     de
            ret

avdc_newline$:
            push    de
            push    bc
            push    hl
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_Y
            add     hl,de
            ld      a,(hl)
            cp      #(AVDC_ROWS - 1)
            jr      nc,avdcn_scroll$
            inc     a
            ld      c,a
            ld      b,#0
            ld      hl,(avdc_tmp_dev$)
            call    avdc_apply_xy$
            jr      avdcn_done$
avdcn_scroll$:
            ld      hl,(avdc_tmp_dev$)
            call    avdc_scroll$
            ld      hl,(avdc_tmp_dev$)
            ld      b,#0
            ld      c,#(AVDC_ROWS - 1)
            call    avdc_apply_xy$
avdcn_done$:
            pop     hl
            pop     bc
            pop     de
            ret

avdc_step_write$:
            push    de
            push    hl
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            inc     (hl)
            ld      a,(hl)
            pop     hl
            cp      #AVDC_COLS
            jr      c,avdcsw_done$
            call    avdc_newline$
avdcsw_done$:
            pop     de
            ret

avdc_step_read$:
            push    de
            push    hl
            ld      hl,(avdc_tmp_dev$)
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            inc     (hl)
            ld      a,(hl)
            cp      #AVDC_COLS
            jr      c,avdcsr_done$
            xor     a
            ld      (hl),a
            inc     hl
            ld      a,(hl)
            inc     a
            cp      #AVDC_ROWS
            jr      c,avdcsr_store_y$
            xor     a
avdcsr_store_y$:
            ld      (hl),a
avdcsr_done$:
            pop     hl
            pop     de
            ret

            ;; ----------------------------------------------------------------
            ;; helper: clear and scroll support
            ;; ----------------------------------------------------------------
avdc_load_attr_a$:
            push    hl
            push    bc
            ld      hl,(avdc_tmp_dev$)
            ld      bc,#DEV_DATA + AVDC_DATA_TEXT_ATTR
            add     hl,bc
            ld      a,(hl)
            pop     bc
            pop     hl
            ret

avdc_fill_cursor_to_ptr$:
            push    af
            call    avdc_set_pointer$
            ld      a,#0x20
            out     (AVDC_PORT_AVDC_DATA),a
            pop     af
            out     (AVDC_PORT_AVDC_ATTR),a
            ld      a,#AVDC_CMD_FILL
            out     (AVDC_PORT_AVDC_STATUS),a
            ret

avdc_clear_row_ptr$:
            push    af
            call    avdc_set_cursor$
            ld      de,#(AVDC_COLS - 1)
            add     hl,de
            pop     af
            call    avdc_fill_cursor_to_ptr$
            ret

avdc_clear_screen$:
            call    avdc_load_attr_a$
            push    af
            ld      hl,#AVDC_TEXT_BASE
            call    avdc_set_cursor$
            ld      hl,#AVDC_CLEAR_END
            pop     af
            call    avdc_fill_cursor_to_ptr$
            ld      hl,(avdc_tmp_dev$)
            ld      b,#0
            ld      c,#0
            call    avdc_apply_xy$
            ret

avdc_scroll$:
            ;; cache row 0's backing pointer because that buffer becomes the new
            ;; bottom line after the row-table rotation.
            ld      l,#0
            call    avdc_wait_mem_acc$
            call    avdc_rowptr_raw$
            ld      (avdc_tmp_rowptr$),hl

            ;; shift row-table entries upward one slot.
            ld      c,#0
            ld      b,#(AVDC_ROWS - 1)
avdcs_roll$:
            ld      a,c
            inc     a
            ld      l,a
            call    avdc_wait_mem_acc$
            call    avdc_rowptr_raw$
            ld      a,c
            add     a,a
            ld      l,a
            ld      h,#0
            call    avdc_set_cursor$
            ld      l,#AVDC_ATTR_NORMAL
            ld      a,e
            call    avdc_putchar$
            ld      a,d
            call    avdc_putchar$
            inc     c
            djnz    avdcs_roll$

            ;; row N-1 now points to the old row 0 backing store.
            ld      l,#(2 * (AVDC_ROWS - 1))
            ld      h,#0
            call    avdc_set_cursor$
            ld      l,#AVDC_ATTR_NORMAL
            ld      a,(avdc_tmp_rowptr$)
            call    avdc_putchar$
            ld      a,(avdc_tmp_rowptr$ + 1)
            call    avdc_putchar$

            ;; clear the recycled bottom-row backing store using the current
            ;; attribute so scrolled-in blank space matches future text output.
            ld      l,#(AVDC_ROWS - 1)
            call    avdc_wait_mem_acc$
            call    avdc_rowptr_raw$
            call    avdc_load_attr_a$
            call    avdc_clear_row_ptr$
            ret

            ;; ----------------------------------------------------------------
            ;; raw AVDC helpers
            ;; ----------------------------------------------------------------
avdc_store_attr$:
            and     #AVDC_ATTR_MASK
            push    af
            push    bc
            ld      bc,#DEV_DATA + AVDC_DATA_TEXT_ATTR
            add     hl,bc
            pop     bc
            pop     af
            ld      (hl),a
            ret

avdc_apply_xy$:
            push    bc
            push    hl
            ld      l,c
            call    avdc_wait_mem_acc$
            call    avdc_rowptr_raw$
            ld      d,#0
            ld      e,b
            add     hl,de
            call    avdc_set_cursor$
            pop     hl
            pop     bc
            push    hl
            ld      de,#DEV_DATA + AVDC_DATA_CUR_X
            add     hl,de
            ld      (hl),b
            inc     hl
            ld      (hl),c
            pop     hl
            ret

avdc_wait_rdy$:
            in      a,(AVDC_PORT_AVDC_STATUS)
            and     #AVDC_STS_RDY
            jr      z,avdc_wait_rdy$
            ret

avdc_wait_mem_acc$:
            in      a,(AVDC_PORT_AVDC_COMMON)
            and     #AVDC_MEM_ACC
            jr      z,avdc_wait_mem_acc$
avdc_wait_mem_acc2$:
            in      a,(AVDC_PORT_AVDC_COMMON)
            and     #AVDC_MEM_ACC
            jr      nz,avdc_wait_mem_acc2$
            ret

avdc_set_pointer$:
            call    avdc_wait_rdy$
            ld      a,#AVDC_CMD_SET_PTR
            out     (AVDC_PORT_AVDC_STATUS),a
            ld      a,l
            out     (AVDC_PORT_AVDC_INIT),a
            ld      a,h
            out     (AVDC_PORT_AVDC_INIT),a
            ret

avdc_read_at_pointer$:
            push    hl
            call    avdc_set_pointer$
            ld      a,#AVDC_CMD_RDPTR
            out     (AVDC_PORT_AVDC_STATUS),a
            call    avdc_wait_rdy$
            in      a,(AVDC_PORT_AVDC_DATA)
            ld      e,a
            in      a,(AVDC_PORT_AVDC_ATTR)
            ld      d,a
            pop     hl
            ret

avdc_rowptr_raw$:
            ld      h,#0
            add     hl,hl
            call    avdc_read_at_pointer$
            push    de
            inc     hl
            call    avdc_read_at_pointer$
            ld      h,e
            pop     de
            ld      l,e
            ld      d,h
            ld      e,l
            ret

avdc_set_cursor$:
            call    avdc_wait_mem_acc$
            call    avdc_wait_rdy$
            ld      a,l
            out     (AVDC_PORT_AVDC_CUR_LO),a
            ld      a,h
            out     (AVDC_PORT_AVDC_CUR_HI),a
            ret

avdc_putchar$:
            ld      (avdc_tmp_ch$),a
            call    avdc_wait_mem_acc$
            call    avdc_wait_rdy$
            ld      a,(avdc_tmp_ch$)
            out     (AVDC_PORT_AVDC_DATA),a
            ld      a,l
            out     (AVDC_PORT_AVDC_ATTR),a
            call    avdc_wait_rdy$
            call    avdc_wait_rdy$
            ld      a,#AVDC_CMD_WAC
            out     (AVDC_PORT_AVDC_STATUS),a
            ret

avdc_cursor_on$:
            call    avdc_wait_rdy$
            ld      a,#AVDC_CMD_CURS_ON
            out     (AVDC_PORT_AVDC_STATUS),a
            ret

avdc_cursor_off$:
            call    avdc_wait_rdy$
            ld      a,#AVDC_CMD_CURS_OFF
            out     (AVDC_PORT_AVDC_STATUS),a
            ret

            ;; driver-level init seeds the immutable port layout once. device
            ;; publication itself is now model-driven in dev.s.
avdc_init::
gdp_init::
            ld      hl,#avdc_dev0$ + DEV_DATA
            ld      (hl),#AVDC_PORT_PIO_COMMON
            inc     hl
            ld      (hl),#AVDC_PORT_EF_CMD
            inc     hl
            ld      (hl),#AVDC_PORT_EF_STATUS
            inc     hl
            ld      (hl),#AVDC_PORT_AVDC_DATA
            inc     hl
            ld      (hl),#AVDC_PORT_AVDC_STATUS
            ld      hl,#DRV_OK
            ret

avdc_dev_drv::
gdp_dev_drv::
            .dw     fd_dev_drv
            .dw     0x0000
            .dw     avdc_init
            .dw     avdc_open
            .dw     drv_close_nop
            .dw     avdc_read
            .dw     avdc_write
            .dw     avdc_ioctl

avdc_init_block$:
            .db     0xd0,0x3e,0xbf,0x05,0x99,0x83,0x0b,0xea,0x00,0x30

avdc_dev0::
avdc_dev0$:
            .dw     0x0000
            .db     'a','v','d','c',0,0
            .db     0x00
            .ds     DEV_DATA_SIZE
            .dw     avdc_dev_drv

            .area   _SYSVARS
avdc_tmp_dev$:
            .ds     2
avdc_tmp_ch$:
            .ds     1
avdc_tmp_attr$:
            .ds     1
avdc_tmp_rowptr$:
            .ds     2

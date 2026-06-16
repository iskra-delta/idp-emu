            ;; avdc.s
            ;;
            ;; minimal rom-side AVDC text output for GDP models.
            ;;
            ;; native calling convention only:
            ;;
            ;;   avdc_print_at():
            ;;     in : a  = PRINT_ATTR_* selector
            ;;          b  = x
            ;;          c  = y
            ;;          hl = zero-terminated text
            ;;
            ;; 2026-06-14   tstih
            .module avdc

            .include "../drivers/gdp.inc"
            .include "print.inc"

            .globl  avdc_init
            .globl  avdc_set_mode
            .globl  avdc_print_at

            .area   _BOOT

            .equ    AVDC_MODE_80,           0x00
            .equ    AVDC_MODE_132,          0x01
            .equ    AVDC_COLS_80,           80
            .equ    AVDC_COLS_132,          132
            .equ    AVDC_TEXT_CTL_80,       0x6d
            .equ    AVDC_TEXT_CTL_132,      0xed

            ;; ----------------------------------------------------------------
            ;; avdc_set_mode(<a> mode)
            ;; ----------------------------------------------------------------
avdc_set_mode::
            ;; the ROM only drives 80-column text. the mode arg is accepted for
            ;; source compatibility but ignored; clearing the ready flag forces
            ;; avdc_init to re-run on the next print.
            xor     a
            ld      (avdc_flags$),a
            ret

            ;; ----------------------------------------------------------------
            ;; avdc_init()
            ;; ----------------------------------------------------------------
avdc_init::
            ld      a,(avdc_flags$)
            or      a
            ret     nz

            ld      a,#0x07
            out     (GDP_PORT_PIO_CTRL_A),a
            out     (GDP_PORT_PIO_CTRL_B),a
            ld      a,#0x0f
            out     (GDP_PORT_PIO_CTRL_A),a
            out     (GDP_PORT_PIO_CTRL_B),a
            ld      a,#0x18
            out     (GDP_PORT_PIO_COMMON),a
            ld      a,#AVDC_TEXT_CTL_80
            out     (GDP_PORT_PIO_TEXT),a

            xor     a
            out     (GDP_PORT_AVDC_STATUS),a
            out     (GDP_PORT_AVDC_COMMON),a
            out     (GDP_PORT_AVDC_CUR_LO),a
            out     (GDP_PORT_AVDC_CUR_HI),a
            out     (GDP_PORT_AVDC_STATUS + 5),a
            out     (GDP_PORT_AVDC_STATUS + 6),a
            out     (GDP_PORT_AVDC_STATUS + 1),a
            out     (GDP_PORT_AVDC_STATUS + 2),a

            ld      a,#0x10
            out     (GDP_PORT_AVDC_STATUS),a
            ld      hl,#avdc_init_block_80$
            ld      c,#GDP_PORT_AVDC_INIT
            ld      b,#10
            otir

            ld      a,#0x3d
            out     (GDP_PORT_AVDC_STATUS),a
            xor     a
            out     (GDP_PORT_AVDC_CUR_LO),a
            out     (GDP_PORT_AVDC_CUR_HI),a

            ;; render the rom text buffer from a known linear base.
            ld      a,#<GDP_TEXT_BASE
            out     (GDP_PORT_AVDC_STATUS + 1),a
            ld      a,#>GDP_TEXT_BASE
            out     (GDP_PORT_AVDC_STATUS + 2),a

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

            ld      hl,#GDP_TEXT_BASE
            call    avdc_set_cursor$

            ;; keep the cursor hidden for the ROM banner/menu path.
            call    avdc_wait_rdy$
            ld      a,#GDP_AVDC_CMD_CURS_OFF
            out     (GDP_PORT_AVDC_STATUS),a

            xor     a
            ld      (avdc_x$),a
            ld      (avdc_y$),a
            inc     a
            ld      (avdc_flags$),a
            ret

            ;; ----------------------------------------------------------------
            ;; avdc_print_at(<a> attr, <b> x, <c> y, <hl> *text)
            ;; ----------------------------------------------------------------
avdc_print_at::
            push    bc
            push    af
            push    hl
            call    avdc_init
            pop     hl
            pop     af
            pop     bc
            ld      d,a
            ld      a,b
            ld      (avdc_x$),a
            ld      a,c
            ld      (avdc_y$),a
            ld      a,d
            call    avdc_attr_to_hw$
            ld      b,a
            call    avdc_apply_xy$

avdc_loop$:
            ld      a,(hl)
            or      a
            ret     z
            inc     hl
            cp      #0x0d
            jr      z,avdc_cr$
            cp      #0x0a
            jr      z,avdc_lf$

            ld      c,a
            call    avdc_wait_mem_acc$
            call    avdc_wait_rdy$
            ld      a,c
            out     (GDP_PORT_AVDC_DATA),a
            ld      a,b
            out     (GDP_PORT_AVDC_ATTR),a
            call    avdc_wait_rdy$
            call    avdc_wait_rdy$
            ld      a,#GDP_AVDC_CMD_WAC
            out     (GDP_PORT_AVDC_STATUS),a

            ld      a,(avdc_x$)
            inc     a
            cp      #AVDC_COLS_80
            jr      c,avdc_store_x$
avdc_wrap_x$:
            xor     a
            ld      (avdc_x$),a
            ld      a,(avdc_y$)
            cp      #GDP_ROWS - 1
            jr      z,avdc_loop$
            inc     a
            ld      (avdc_y$),a
            call    avdc_apply_xy$
            jr      avdc_loop$

avdc_store_x$:
            ld      (avdc_x$),a
            jr      avdc_loop$

avdc_cr$:
            xor     a
            ld      (avdc_x$),a
            call    avdc_apply_xy$
            jr      avdc_loop$

avdc_lf$:
            ld      a,(avdc_y$)
            cp      #GDP_ROWS - 1
            jr      z,avdc_loop$
            inc     a
            ld      (avdc_y$),a
            call    avdc_apply_xy$
            jr      avdc_loop$

avdc_attr_to_hw$:
            cp      #PRINT_ATTR_HIGHLIGHT
            jr      z,avdc_attr_hi$
            cp      #PRINT_ATTR_INVERSE
            jr      z,avdc_attr_inv$
            xor     a
            ret
avdc_attr_hi$:
            ld      a,#GDP_ATTR_HIGHLIGHT
            ret
avdc_attr_inv$:
            ;; make the active menu field unmistakable on the GDP mono path:
            ;; inverse + highlight yields a bright block with dark glyphs.
            ld      a,#(GDP_ATTR_HIGHLIGHT | GDP_ATTR_INVERSE)
            ret

avdc_apply_xy$:
            push    hl
            ld      a,(avdc_y$)
            ld      l,a
            ld      h,#0
            add     hl,hl               ; 2*y
            add     hl,hl               ; 4*y
            add     hl,hl               ; 8*y
            add     hl,hl               ; 16*y
            push    hl
            add     hl,hl               ; 32*y
            add     hl,hl               ; 64*y
            pop     de                  ; 16*y
            add     hl,de               ; 80*y
            ld      de,#GDP_TEXT_BASE
            add     hl,de
            ld      a,(avdc_x$)
            ld      e,a
            ld      d,#0
            add     hl,de
            call    avdc_set_cursor$
            pop     hl
            ret

avdc_set_cursor$:
            call    avdc_wait_mem_acc$
            call    avdc_wait_rdy$
            ld      a,l
            out     (GDP_PORT_AVDC_CUR_LO),a
            ld      a,h
            out     (GDP_PORT_AVDC_CUR_HI),a
            ret

avdc_wait_rdy$:
            in      a,(GDP_PORT_AVDC_STATUS)
            and     #GDP_AVDC_STS_RDY
            jr      z,avdc_wait_rdy$
            ret

avdc_wait_mem_acc$:
            in      a,(GDP_PORT_AVDC_COMMON)
            and     #GDP_AVDC_MEM_ACC
            jr      z,avdc_wait_mem_acc$
avdc_wait_mem_acc2$:
            in      a,(GDP_PORT_AVDC_COMMON)
            and     #GDP_AVDC_MEM_ACC
            jr      nz,avdc_wait_mem_acc2$
            ret

            ;; rom-time 80-column AVDC init profile
avdc_init_block_80$:
            .db     0xd0,0x2f,0x0d,0x05,0x99,0x4f,0x0a,0xea,0x00,0x30

            .area   _SYSVARS

avdc_flags$:
            .db     0x00

avdc_x$:
            .db     0x00

avdc_y$:
            .db     0x00

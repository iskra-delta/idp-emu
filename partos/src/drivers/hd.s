            ;; hd.s
            ;;
            ;; partner hard disk bios driver
            ;;
            ;; transfers use a flat 24-bit byte cursor stored in dev.data[].
            ;; the current implementation accepts counts and offsets aligned to
            ;; the 256-byte physical block size and maps them to raw SASI
            ;; READ(6)/WRITE(6) commands automatically.
            ;;
            ;; 2026-06-13   tstih
            .module hd

            .include "dev.inc"
            .include "drv.inc"
            .include "hd.inc"

            .globl  delay_1ms
            .globl  drv_reset_dev
            .globl  drv_open_pos0
            .globl  drv_close_nop
            .globl  drv_prep_rw256
            .globl  drv_advance_pos256
            .globl  drv_ioctl_pos24
            .globl  hd_probe

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *chain <= hd_probe()
            ;; ----------------------------------------------------------------
            ;; probes the raw partner sasi adapter exactly through the real
            ;; ports. with no controller on the bus reads float at 0xff, so
            ;; every wait path rejects that value before accepting status bits.
            ;; a successful probe performs the minimal select -> command phase
            ;; -> 1-byte ready exchange -> response phase handshake and returns
            ;; a single sda device instance.
            ;; ----------------------------------------------------------------
hd_probe::
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jp      z,hdp_none$

            xor     a
            out     (HD_PORT_RESET),a
            ld      c,#HD_RESET_DELAY_MS
hdp_delay$:
            ld      hl,#0x0000
            call    delay_1ms
            dec     c
            jr      nz,hdp_delay$

            call    hd_begin_session$
            jp      c,hdp_none$
            ld      a,#HD_CMD_READY
            call    hd_write_cmd_byte$
            jp      c,hdp_fail$
            call    hd_finish_cmd$
            jp      c,hdp_fail$
            call    hd_end_session$

            ld      hl,#hd_dev0$
            ld      de,#hd_dev_drv
            call    drv_reset_dev

            push    hl
            ld      de,#DEV_DATA
            add     hl,de
            xor     a
            ld      (hl),a              ; target 0
            pop     hl

            push    hl
            ld      de,#DEV_NAME
            add     hl,de
            ld      a,#'s'
            ld      (hl),a
            inc     hl
            ld      a,#'d'
            ld      (hl),a
            inc     hl
            ld      a,#'a'
            ld      (hl),a
            pop     hl
            ret

hd_open::
            jp      drv_open_pos0

hd_read::
            ld      a,b
            or      c
            jr      nz,hdr_go$
            ld      hl,#DRV_OK
            ret
hdr_go$:
            ld      (hd_io_ptr$),de
            ld      a,b
            ld      (hd_xfer_count$),a
            push    hl
            call    drv_prep_rw256
            ld      a,h
            or      l
            jr      nz,hdr_prep_fail$
            pop     hl
            call    hd_begin_session$
            jp      c,hd_err$
            ld      a,#HD_CMD_READ6
            call    hd_issue_rw6$
            jr      c,hdr_fail_end$
            ld      de,(hd_io_ptr$)
            ld      a,(hd_xfer_count$)
hdr_blk$:
            push    af
            ld      c,#0
hdr_byte$:
            call    hd_read_data_byte$
            jr      c,hdr_data_fail$
            ld      (de),a
            inc     de
            dec     c
            jr      nz,hdr_byte$
            pop     af
            dec     a
            jr      nz,hdr_blk$
            ld      (hd_io_ptr$),de
            call    hd_finish_cmd$
            jr      c,hdr_fail_end$
            call    hd_end_session$
            call    hd_advance_cursor$
            ld      hl,#DRV_OK
            ret
hdr_data_fail$:
            pop     af
hdr_fail_end$:
            call    hd_end_session$
            jp      hd_err$
hdr_prep_fail$:
            pop     de
            ret

hd_write::
            ld      a,b
            or      c
            jr      nz,hdw_go$
            ld      hl,#DRV_OK
            ret
hdw_go$:
            ld      (hd_io_ptr$),de
            ld      a,b
            ld      (hd_xfer_count$),a
            push    hl
            call    drv_prep_rw256
            ld      a,h
            or      l
            jr      nz,hdw_prep_fail$
            pop     hl
            call    hd_begin_session$
            jp      c,hd_err$
            ld      a,#HD_CMD_WRITE6
            call    hd_issue_rw6$
            jr      c,hdw_fail_end$
            ld      de,(hd_io_ptr$)
            ld      a,(hd_xfer_count$)
hdw_blk$:
            push    af
            ld      c,#0
hdw_byte$:
            ld      a,(de)
            call    hd_write_data_byte$
            jr      c,hdw_data_fail$
            inc     de
            dec     c
            jr      nz,hdw_byte$
            pop     af
            dec     a
            jr      nz,hdw_blk$
            ld      (hd_io_ptr$),de
            call    hd_finish_cmd$
            jr      c,hdw_fail_end$
            call    hd_end_session$
            call    hd_advance_cursor$
            ld      hl,#DRV_OK
            ret
hdw_data_fail$:
            pop     af
hdw_fail_end$:
            call    hd_end_session$
            jp      hd_err$
hdw_prep_fail$:
            pop     de
            ret

hd_ioctl::
            jp      drv_ioctl_pos24

hd_begin_session$:
            ld      a,#HD_CTRL_SEL
            out     (HD_PORT_CTRL),a
            call    hd_wait_bsy$
            ret     c
            ld      a,#HD_CTRL_DATA_EN | HD_CTRL_DRQ_EN
            out     (HD_PORT_CTRL),a
            or      a
            ret

hd_end_session$:
            xor     a
            out     (HD_PORT_CTRL),a
            ret

hd_issue_rw6$:
            push    af
            call    hd_write_cmd_byte$
            jr      c,hdirw_fail$
            push    de
            push    bc
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_UNIT
            add     hl,bc
            ld      a,(hl)
            pop     hl
            and     #0x07
            add     a,a
            add     a,a
            add     a,a
            call    hd_write_cmd_byte$
            jr      c,hdirw_fail_pop2$
            pop     bc
            pop     de
            ld      a,d
            call    hd_write_cmd_byte$
            jr      c,hdirw_fail$
            ld      a,e
            call    hd_write_cmd_byte$
            jr      c,hdirw_fail$
            ld      a,b
            call    hd_write_cmd_byte$
            jr      c,hdirw_fail$
            xor     a
            call    hd_write_cmd_byte$
            pop     af
            ret
hdirw_fail_pop2$:
            pop     bc
            pop     de
hdirw_fail$:
            pop     af
            scf
            ret

hd_advance_cursor$:
            ld      a,(hd_xfer_count$)
hdac_loop$:
            or      a
            ret     z
            push    af
            call    drv_advance_pos256
            pop     af
            dec     a
            jr      hdac_loop$

hd_write_cmd_byte$:
            ld      e,a
            call    hd_wait_cmd_phase$
            ret     c
            ld      a,e
            out     (HD_PORT_DATA),a
            or      a
            ret

hd_write_data_byte$:
            ld      e,a
            call    hd_wait_data_out$
            ret     c
            ld      a,e
            out     (HD_PORT_DATA),a
            or      a
            ret

hd_read_data_byte$:
            call    hd_wait_data_in$
            ret     c
            in      a,(HD_PORT_DATA)
            or      a
            ret

hd_finish_cmd$:
            call    hd_read_resp_byte$
            ret     c
            ld      d,a
            call    hd_read_resp_byte$
            ret     c
            ld      a,d
            or      a
            ret     z
            scf
            ret

hd_read_resp_byte$:
            call    hd_wait_resp_phase$
            ret     c
            in      a,(HD_PORT_DATA)
            or      a
            ret

hd_wait_bsy$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_bsy$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_BSY
            ret     nz
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_bsy$
hdw_fail$:
            scf
            ret

hd_wait_cmd_phase$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_cmd$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
            cp      #HD_STATUS_CMD
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_cmd$
            jr      hdw_fail$

hd_wait_data_in$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_datin$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
            cp      #HD_STATUS_DATA_IN
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_datin$
            jr      hdw_fail$

hd_wait_data_out$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_datout$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
            cp      #HD_STATUS_DATA_OUT
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_datout$
            jr      hdw_fail$

hd_wait_resp_phase$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_resp$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
            cp      #HD_STATUS_RESP
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_resp$
            jr      hdw_fail$

hd_err$:
            ld      hl,#DRV_ERR
            ret

hd_dev_drv::
            .dw     0x0000
            .dw     hd_probe
            .dw     hd_open
            .dw     drv_close_nop
            .dw     hd_read
            .dw     hd_write
            .dw     hd_ioctl

hd_dev0$:
            .dw     0x0000
            .db     's','d','a',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     hd_dev_drv

hd_io_ptr$:
            .dw     0x0000

hd_xfer_count$:
            .db     0x00

hdp_fail$:
            call    hd_end_session$
hdp_none$:
            ld      hl,#0x0000
            ret

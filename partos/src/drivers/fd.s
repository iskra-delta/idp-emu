            ;; fd.s
            ;;
            ;; partner floppy disk bios driver
            ;;
            ;; transfers use a flat 24-bit byte cursor stored in dev.data[].
            ;; the current implementation accepts counts and offsets aligned to
            ;; the 256-byte physical sector size and maps them to raw i8272
            ;; CHS accesses automatically.
            ;;
            ;; 2026-06-13   tstih
            .module fd

            .include "dev.inc"
            .include "drv.inc"
            .include "fd.inc"

            .globl  drv_reset_dev
            .globl  drv_open_pos0
            .globl  drv_close_nop
            .globl  drv_prep_rw256
            .globl  drv_advance_pos256
            .globl  drv_ioctl_pos24

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> *chain <= fd_probe()
            ;; ----------------------------------------------------------------
            ;; probes the raw i8272 for ready units by issuing SENSE DRIVE
            ;; STATUS to all four unit numbers. detected devices are chained
            ;; as fd0, fd1, ... in discovery order, while dev.data[0] keeps
            ;; the physical fdc unit number needed later by real I/O code.
            ;; ----------------------------------------------------------------
fd_probe::
            xor     a
            ld      (fd_probe_count$),a
            ld      l,a
            ld      h,a
            ld      (fd_probe_head$),hl
            ld      (fd_probe_tail$),hl

            ld      b,#0
fdp_loop$:
            push    bc
            call    fd_probe_unit$
            pop     bc
            jr      c,fdp_done$
            inc     b
            ld      a,b
            cp      #FD_MAX_UNITS
            jr      c,fdp_loop$
fdp_done$:
            ld      hl,(fd_probe_head$)
            ret

fd_probe_unit$:
            ld      d,b
            ld      a,#FD_CMD_SENSE_DRIVE
            call    fd_write_data$
            ret     c
            ld      a,d
            and     #0x03
            call    fd_write_data$
            ret     c
            call    fd_read_data$
            ret     c
            and     #FD_ST3_READY
            ret     z

            ld      b,d
            call    fd_dev_by_unit$
            push    bc
            ld      de,#fd_dev_drv
            call    drv_reset_dev
            pop     bc

            push    hl
            ld      de,#DEV_DATA
            add     hl,de
            ld      a,b
            ld      (hl),a
            pop     hl

            push    hl
            ld      de,#DEV_NAME
            add     hl,de
            ld      a,#'f'
            ld      (hl),a
            inc     hl
            ld      a,#'d'
            ld      (hl),a
            inc     hl
            ld      a,(fd_probe_count$)
            add     a,#'0'
            ld      (hl),a
            pop     de

            ld      hl,(fd_probe_head$)
            ld      a,h
            or      l
            jr      nz,fdp_link$
            ld      (fd_probe_head$),de
            jr      fdp_tail$
fdp_link$:
            ld      hl,(fd_probe_tail$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
fdp_tail$:
            ld      (fd_probe_tail$),de
            ld      a,(fd_probe_count$)
            inc     a
            ld      (fd_probe_count$),a
            or      a
            ret

fd_open::
            push    hl
            call    drv_open_pos0
            pop     hl
            call    fd_recal_dev$
            jp      c,fd_err$
            ld      hl,#DRV_OK
            ret

fd_read::
            ld      a,b
            or      c
            jr      nz,fdr_go$
            ld      hl,#DRV_OK
            ret
fdr_go$:
            ld      (fd_io_ptr$),de
fdr_loop$:
            push    bc
            push    hl
            call    drv_prep_rw256
            ld      a,h
            or      l
            jr      nz,fdr_prep_fail$
            pop     hl
            call    fd_lba_to_chs$
            ld      de,(fd_io_ptr$)
            call    fd_read_sector$
            jr      c,fdr_xfer_fail$
            ld      (fd_io_ptr$),de
            call    drv_advance_pos256
            pop     bc
            djnz    fdr_loop$
            ld      hl,#DRV_OK
            ret
fdr_prep_fail$:
            pop     de
            pop     bc
            ret
fdr_xfer_fail$:
            pop     bc
            jp      fd_err$

fd_write::
            ld      a,b
            or      c
            jr      nz,fdw_go$
            ld      hl,#DRV_OK
            ret
fdw_go$:
            ld      (fd_io_ptr$),de
fdw_loop$:
            push    bc
            push    hl
            call    drv_prep_rw256
            ld      a,h
            or      l
            jr      nz,fdw_prep_fail$
            pop     hl
            call    fd_lba_to_chs$
            ld      de,(fd_io_ptr$)
            call    fd_write_sector$
            jr      c,fdw_xfer_fail$
            ld      (fd_io_ptr$),de
            call    drv_advance_pos256
            pop     bc
            djnz    fdw_loop$
            ld      hl,#DRV_OK
            ret
fdw_prep_fail$:
            pop     de
            pop     bc
            ret
fdw_xfer_fail$:
            pop     bc
            jp      fd_err$

fd_ioctl::
            jp      drv_ioctl_pos24

fd_lba_to_chs$:
            ld      c,#0
fdc_cyl$:
            ld      a,d
            or      a
            jr      nz,fdc_sub36$
            ld      a,e
            cp      #FD_SECTRK * FD_HEADS
            jr      c,fdc_head$
fdc_sub36$:
            ld      a,e
            sub     #FD_SECTRK * FD_HEADS
            ld      e,a
            jr      nc,fdc_noborrow$
            dec     d
fdc_noborrow$:
            inc     c
            jr      fdc_cyl$
fdc_head$:
            ld      b,#0
            ld      a,e
            cp      #FD_SECTRK
            jr      c,fdc_sector$
            sub     #FD_SECTRK
            ld      e,a
            ld      b,#1
fdc_sector$:
            ld      a,e
            inc     a
            ret

fd_get_hu$:
            push    hl
            push    bc
            ld      bc,#DEV_DATA + DRV_DATA_UNIT
            add     hl,bc
            pop     bc
            ld      a,b
            add     a,a
            add     a,a
            or      (hl)
            pop     hl
            ret

fd_recal_dev$:
            push    hl
            ld      a,#FD_CMD_RECAL
            call    fd_write_data$
            jr      c,fdrc_fail$
            ld      bc,#DEV_DATA + DRV_DATA_UNIT
            add     hl,bc
            ld      a,(hl)
            and     #0x03
            call    fd_write_data$
            jr      c,fdrc_fail$
            call    fd_wait_int$
            pop     hl
            ret
fdrc_fail$:
            pop     hl
            scf
            ret

fd_seek_dev$:
            push    af
            ld      a,#FD_CMD_SEEK
            call    fd_write_data$
            jr      c,fds_fail$
            call    fd_get_hu$
            call    fd_write_data$
            jr      c,fds_fail$
            ld      a,c
            call    fd_write_data$
            jr      c,fds_fail$
            call    fd_wait_int$
            pop     af
            ret
fds_fail$:
            pop     af
            scf
            ret

fd_wait_int$:
            ld      b,#64
fdwi_loop$:
            push    bc
            ld      a,#FD_CMD_SENSE_INT
            call    fd_write_data$
            jr      c,fdwi_fail_pop$
            call    fd_read_data$
            jr      c,fdwi_fail_pop$
            cp      #FD_ST0_IC_IC
            jr      nz,fdwi_got$
            pop     bc
            djnz    fdwi_loop$
            scf
            ret
fdwi_got$:
            ld      d,a
            call    fd_read_data$
            pop     bc
            ld      a,d
            and     #FD_ST0_IC_MASK
            ret     z
            scf
            ret
fdwi_fail_pop$:
            pop     bc
            scf
            ret

fd_finish_rw$:
            call    fd_read_data$
            ret     c
            ld      d,a
            ld      b,#6
fdf_res$:
            call    fd_read_data$
            ret     c
            djnz    fdf_res$
            ld      a,d
            and     #FD_ST0_IC_MASK
            ret     z
            scf
            ret

fd_read_sector$:
            push    af
            call    fd_seek_dev$
            jr      c,fdrs_fail$
            ld      a,#FD_CMD_READ_DATA
            call    fd_write_data$
            jr      c,fdrs_fail$
            call    fd_get_hu$
            call    fd_write_data$
            jr      c,fdrs_fail$
            ld      a,c
            call    fd_write_data$
            jr      c,fdrs_fail$
            ld      a,b
            call    fd_write_data$
            jr      c,fdrs_fail$
            pop     af
            push    af
            call    fd_write_data$
            jr      c,fdrs_fail$
            ld      a,#FD_SECTOR_N
            call    fd_write_data$
            jr      c,fdrs_fail$
            pop     af
            push    af
            call    fd_write_data$
            jr      c,fdrs_fail$
            ld      a,#FD_RW_GPL
            call    fd_write_data$
            jr      c,fdrs_fail$
            ld      a,#FD_RW_DTL
            call    fd_write_data$
            jr      c,fdrs_fail$
            pop     af
            ld      c,#0
fdrs_data$:
            call    fd_read_data$
            jr      c,fdrs_fail_nc$
            ld      (de),a
            inc     de
            dec     c
            jr      nz,fdrs_data$
            call    fd_finish_rw$
            ret
fdrs_fail$:
            pop     af
fdrs_fail_nc$:
            scf
            ret

fd_write_sector$:
            push    af
            call    fd_seek_dev$
            jr      c,fdws_fail$
            ld      a,#FD_CMD_WRITE_DATA
            call    fd_write_data$
            jr      c,fdws_fail$
            call    fd_get_hu$
            call    fd_write_data$
            jr      c,fdws_fail$
            ld      a,c
            call    fd_write_data$
            jr      c,fdws_fail$
            ld      a,b
            call    fd_write_data$
            jr      c,fdws_fail$
            pop     af
            push    af
            call    fd_write_data$
            jr      c,fdws_fail$
            ld      a,#FD_SECTOR_N
            call    fd_write_data$
            jr      c,fdws_fail$
            pop     af
            push    af
            call    fd_write_data$
            jr      c,fdws_fail$
            ld      a,#FD_RW_GPL
            call    fd_write_data$
            jr      c,fdws_fail$
            ld      a,#FD_RW_DTL
            call    fd_write_data$
            jr      c,fdws_fail$
            pop     af
            ld      c,#0
fdws_data$:
            ld      a,(de)
            call    fd_write_data$
            jr      c,fdws_fail_nc$
            inc     de
            dec     c
            jr      nz,fdws_data$
            call    fd_finish_rw$
            ret
fdws_fail$:
            pop     af
fdws_fail_nc$:
            scf
            ret

fd_dev_by_unit$:
            ld      hl,#fd_dev0$
            ld      a,b
            or      a
            ret     z
            dec     a
            ld      hl,#fd_dev1$
            ret     z
            dec     a
            ld      hl,#fd_dev2$
            ret     z
            ld      hl,#fd_dev3$
            ret

fd_write_data$:
            ld      e,a
            call    fd_wait_cmd_ready$
            ret     c
            ld      a,e
            out     (FD_PORT_DATA),a
            or      a
            ret

fd_read_data$:
            call    fd_wait_res_ready$
            ret     c
            in      a,(FD_PORT_DATA)
            or      a
            ret

fd_wait_cmd_ready$:
            ld      bc,#FD_POLL_TIMEOUT
fdw_cmd$:
            in      a,(FD_PORT_MSR)
            and     #FD_MSR_PHASE_MASK
            cp      #FD_MSR_CMD_READY
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,fdw_cmd$
            scf
            ret

fd_wait_res_ready$:
            ld      bc,#FD_POLL_TIMEOUT
fdw_res$:
            in      a,(FD_PORT_MSR)
            and     #FD_MSR_PHASE_MASK
            cp      #FD_MSR_RES_READY
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,fdw_res$
            scf
            ret

fd_err$:
            ld      hl,#DRV_ERR
            ret

fd_dev_drv::
            .dw     0x0000
            .dw     fd_probe
            .dw     fd_open
            .dw     drv_close_nop
            .dw     fd_read
            .dw     fd_write
            .dw     fd_ioctl

fd_dev0$:
            .dw     0x0000
            .db     'f','d','0',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     fd_dev_drv

fd_dev1$:
            .dw     0x0000
            .db     'f','d','0',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     fd_dev_drv

fd_dev2$:
            .dw     0x0000
            .db     'f','d','0',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     fd_dev_drv

fd_dev3$:
            .dw     0x0000
            .db     'f','d','0',0,0,0,0,0
            .db     0x00
            .db     0x00
            .ds     16
            .dw     fd_dev_drv

fd_probe_head$:
            .dw     0x0000

fd_probe_tail$:
            .dw     0x0000

fd_io_ptr$:
            .dw     0x0000

fd_probe_count$:
            .db     0x00

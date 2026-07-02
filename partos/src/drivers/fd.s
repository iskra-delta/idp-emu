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

            .globl  drv_open_pos0
            .globl  drv_close_nop
            .globl  drv_prep_rw256
            .globl  drv_advance_pos256
            .globl  drv_ioctl_pos24
            .globl  drv_err
            .globl  drv_signal_done
            .globl  drv_zero_ok_bc_ix
            .globl  drv_isr_enter
            .globl  drv_isr_exit
            .globl  __sys_nvram_cache
            .globl  fd_init
            .globl  fd_dev0
            .globl  fd_dev1
            .globl  fd_dev2
            .globl  fd_dev3
            .globl  _ir_set
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  ir_refcnt
            .globl  _fd_isr
            .globl  hd_dev_drv

            .area   _CODE

            ;; per floppy type (1..3): sectors/track, fdc sector-size code,
            ;; sector size >> 8. the rom delegates this geometry to the os.
fd_geom_table$:
            .db     18,0x01,0x01        ; 1: PARTNER  18 spt, 256-byte
            .db      9,0x02,0x02        ; 2: DOS-720K  9 spt, 512-byte
            .db      9,0x02,0x02        ; 3: DOS-360K  9 spt, 512-byte

fd_seed_geom$:                          ; hl=dev, a=type (0..3)
            or      a
            ret     z
            push    hl
            dec     a
            ld      l,a
            ld      h,#0
            ld      e,a
            ld      d,#0
            add     hl,hl
            add     hl,de
            ld      de,#fd_geom_table$
            add     hl,de
            ex      de,hl               ; de = geometry entry
            pop     hl                  ; hl = dev
            ld      bc,#DEV_DATA + FD_DATA_SPT
            add     hl,bc
            ld      a,(de)
            ld      (hl),a
            inc     de
            inc     hl
            ld      a,(de)
            ld      (hl),a
            inc     de
            inc     hl
            ld      a,(de)
            ld      (hl),a
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= fd_init()
            ;; ----------------------------------------------------------------
            ;; driver-level i8272 setup, run once at kernel start. the rom uses
            ;; polled i/o, so the controller is NOT in dma mode: we issue
            ;; SPECIFY (nd = 0) to switch it to dma transfers, then install the
            ;; kernel's completion-interrupt handler into the im 2 table.
            ;; per-unit recalibrate is in fd_open.
            ;; ----------------------------------------------------------------
fd_init::
            ld      a,#FD_CMD_SPECIFY
            call    fd_write_data$
            ld      a,#FD_SPEC_SRT_HUT
            call    fd_write_data$
            ld      a,#FD_SPEC_HLT_ND       ; nd = 0 -> dma data transfers
            call    fd_write_data$
            ;; install the fdc completion-interrupt handler
            ld      a,#FD_VEC
            out     (FD_PORT_INT_VEC),a     ; fdc im 2 vector register
            ld      a,#FD_VEC
            ld      de,#_fd_isr
            call    _ir_set
            ld      a,(__sys_nvram_cache + FD_NVRAM_TYPE_BYTE)
            rlca
            rlca
            and     #0x03
            ld      hl,#fd_dev0$
            call    fd_seed_geom$
            ld      a,(__sys_nvram_cache + FD_NVRAM_TYPE_BYTE)
            and     #0x30
            rrca
            rrca
            rrca
            rrca
            ld      hl,#fd_dev1$
            call    fd_seed_geom$
            ld      a,(__sys_nvram_cache + FD_NVRAM_TYPE_BYTE)
            and     #0x0c
            rrca
            rrca
            ld      hl,#fd_dev2$
            call    fd_seed_geom$
            ld      a,(__sys_nvram_cache + FD_NVRAM_TYPE_BYTE)
            and     #0x03
            ld      hl,#fd_dev3$
            call    fd_seed_geom$
            ld      hl,#DRV_OK
            ret

fd_open::
            push    hl
            call    drv_open_pos0
            pop     hl
            ;; spin the drive path up (OUT 0x98). real partner + emulator both
            ;; require the motor before the controller will service a seek, so
            ;; the recalibrate below would otherwise never complete.
            xor     a
            out     (FD_PORT_MOTOR),a
            ;; an enumerated floppy drive can still be empty. detect that up
            ;; front so callers get DRV_ERR instead of waiting forever on a
            ;; completion event that no read will ever signal.
            call    fd_sense_dev$
            jr      c,fdo_fail$
            and     #FD_ST3_READY
            jr      nz,fdo_ready$
fdo_fail$:
            jp      drv_err
fdo_ready$:
            ;; mask the fdc completion interrupt across the recalibrate: the
            ;; recal waits via a polled SENSE-INTERRUPT loop, and the installed
            ;; _fd_isr would otherwise race it and consume the seek-complete
            ;; status. ir_disable preserves hl; ir_enable preserves af, so the
            ;; recal's carry result survives.
            call    _ir_disable
            call    fd_recal_dev$
            call    _ir_enable
            jp      c,drv_err
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= fd_read (<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; <hl> rc <= fd_write(<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; asynchronous, dma + interrupt driven. records the request, then
            ;; arms the first sector; the fdc completion isr (fd_isr) moves the
            ;; transfer forward sector by sector and signals the event when the
            ;; whole request is done. read and write share everything except the
            ;; dma direction and the fdc command (fd_io_dir$). returns DRV_OK
            ;; once started, DRV_ERR on a bad (mis-aligned) request.
            ;; ----------------------------------------------------------------
fd_read::
            call    drv_zero_ok_bc_ix
            ret     z
            xor     a                   ; dir = 0 (read)
            jr      fd_xfer_start$

fd_write::
            call    drv_zero_ok_bc_ix
            ret     z
            ld      a,#1                ; dir = 1 (write)

fd_xfer_start$:                         ; a=dir, hl=dev, de=buf, bc=count, ix=event
            ;; motor may have timed out since fd_open(); spin it up again
            ;; before arming another dma transfer.
            push    af
            xor     a
            out     (FD_PORT_MOTOR),a
            pop     af
            ld      (fd_io_dir$),a
            ld      (fd_io_dev$),hl
            ld      (fd_io_buf$),de
            push    ix
            pop     de
            ld      (fd_io_event$),de
            xor     a
            ld      (fd_io_err$),a
            ld      a,#0xff
            ld      (fd_io_cur_cyl$),a  ; force a seek on the first sector
            ;; load this unit's geometry (spt / sector-size code / size>>8)
            push    bc                  ; save count
            ld      de,#DEV_DATA + FD_DATA_SPT
            add     hl,de
            ld      a,(hl)
            ld      (fd_io_spt$),a
            inc     hl
            ld      a,(hl)
            ld      (fd_io_n$),a
            inc     hl
            ld      a,(hl)
            ld      (fd_io_ssh$),a
            ld      hl,(fd_io_dev$)     ; hl = dev
            pop     bc                  ; bc = count
            ;; validate 256-alignment and fetch the 256-block index
            call    drv_prep_rw256      ; de = offset>>8, b = count>>8, hl = rc
            ld      a,h
            or      l
            ret     nz                  ; bad params -> DRV_ERR, no event
            ;; advance the byte cursor over the whole request (b 256-byte blocks)
            push    de                  ; 256-block index
            push    bc                  ; 256-block count
fdx_adv$:
            push    bc
            ld      hl,(fd_io_dev$)
            call    drv_advance_pos256
            pop     bc
            djnz    fdx_adv$
            pop     bc                  ; b = 256-block count
            pop     de                  ; de = 256-block index
            ;; convert to the unit's sectors: 512-byte sectors halve lba+count
            ld      a,(fd_io_ssh$)
            cp      #2
            jr      nz,fdx_have$
            srl     d
            rr      e                   ; sector lba = (offset>>8) >> 1
            srl     b                   ; sector count = (count>>8) >> 1
fdx_have$:
            ld      (fd_io_lba$),de
            ld      a,b
            ld      (fd_io_left$),a     ; sectors remaining
            ;; arm the first sector with interrupts masked so the seek's
            ;; sense-int handshake cannot re-enter the fdc handler mid-setup.
            call    _ir_disable
            call    fd_dma_start_sector$
            call    _ir_enable
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fd_dma_start_sector$()
            ;; ----------------------------------------------------------------
            ;; seeks (only if the cylinder changed), programs the dma for the
            ;; current sector's buffer and issues the dma-mode read/write
            ;; command. expects interrupts masked. uses the fd_io_* state.
            ;; ----------------------------------------------------------------
fd_dma_start_sector$:
            ld      de,(fd_io_lba$)
            call    fd_lba_to_chs$      ; c = cyl, b = head, a = sector
            ld      (fd_io_sec$),a
            ld      a,b
            ld      (fd_io_head$),a
            ld      a,c
            ld      (fd_io_cyl$),a
            ld      hl,#fd_io_cur_cyl$
            cp      (hl)                ; same cylinder as last time?
            jr      z,fdss_ready$
            ld      (hl),a              ; remember new cylinder
            ld      hl,(fd_io_dev$)
            call    fd_seek_dev$        ; hl=dev, b=head, c=cyl (polled)
fdss_ready$:
            ld      hl,(fd_io_buf$)
            call    fd_dma_setup$       ; program the dma for this sector
            jp      fd_dma_issue_cmd$   ; send the dma-mode read/write command

            ;; ----------------------------------------------------------------
            ;; fd_dma_setup$(<hl> buffer)
            ;; ----------------------------------------------------------------
            ;; programs the z80 dma to move one 256-byte sector between the fdc
            ;; data port (port b, fixed) and the buffer (port a, increment). the
            ;; direction (wr0 0x79 read = b->a, 0x7d write = a->b) follows
            ;; fd_io_dir$. this is the exact boot-rom byte stream the emulator's
            ;; dma model understands, parameterized by buffer and direction.
            ;; ----------------------------------------------------------------
fd_dma_setup$:
            ld      a,#0x05
            out     (FD_PORT_DMA),a     ; reset dma
            ld      a,#0xcf
            out     (FD_PORT_DMA),a
            ld      a,(fd_io_dir$)
            or      a
            ld      a,#0x79             ; read: port b (fdc) -> port a (mem)
            jr      z,fdds_wr0$
            ld      a,#0x7d             ; write: port a (mem) -> port b (fdc)
fdds_wr0$:
            out     (FD_PORT_DMA),a     ; wr0: direction, addr+len follow
            ld      a,l
            out     (FD_PORT_DMA),a     ; buffer low
            ld      a,h
            out     (FD_PORT_DMA),a     ; buffer high
            xor     a
            out     (FD_PORT_DMA),a     ; length low = 0
            ld      a,(fd_io_ssh$)
            out     (FD_PORT_DMA),a     ; length high = ssh (1->256 B, 2->512 B)
            ld      hl,#fd_dma_cfg$
            ld      b,#9
            ld      c,#FD_PORT_DMA
            otir                        ; wr1/wr2 + fixed-io port + enable/load
            ret

fd_dma_cfg$:
            .db     0x14,0x28,0x85,FD_PORT_DATA,0x8a,0xcf,0x01,0xcf,0x87

            ;; ----------------------------------------------------------------
            ;; fd_dma_issue_cmd$()
            ;; ----------------------------------------------------------------
            ;; sends the i8272 READ/WRITE DATA command for one sector (per
            ;; fd_io_dir$). in dma mode the controller moves the 256 data bytes
            ;; through the dma; no inline data phase. completion raises the fdc
            ;; interrupt (fd_isr).
            ;; ----------------------------------------------------------------
fd_dma_issue_cmd$:
            ld      a,(fd_io_dir$)
            or      a
            ld      a,#FD_CMD_READ_DATA
            jr      z,fddi_cmd$
            ld      a,#FD_CMD_WRITE_DATA
fddi_cmd$:
            call    fd_write_data$
            ld      hl,(fd_io_dev$)
            ld      a,(fd_io_head$)
            ld      b,a
            call    fd_get_hu$          ; head/unit byte
            call    fd_write_data$
            ld      a,(fd_io_cyl$)
            call    fd_write_data$      ; C
            ld      a,(fd_io_head$)
            call    fd_write_data$      ; H
            ld      a,(fd_io_sec$)
            call    fd_write_data$      ; R
            ld      a,(fd_io_n$)
            call    fd_write_data$      ; N (sector size code)
            ld      a,(fd_io_sec$)
            call    fd_write_data$      ; EOT = R (one sector)
            ld      a,#FD_RW_GPL
            call    fd_write_data$      ; GPL
            ld      a,#FD_RW_DTL
            jp      fd_write_data$      ; DTL (tail)

            ;; ----------------------------------------------------------------
            ;; _fd_isr()  -- im 2 fdc completion handler
            ;; ----------------------------------------------------------------
            ;; runs when a dma sector transfer finishes. reads the result phase,
            ;; advances the transfer, arms the next sector, or signals the
            ;; completion event when the whole request is done. holds the ir
            ;; bracket because drv_signal_done -> evt_set walks kernel lists.
            ;; ----------------------------------------------------------------
_fd_isr::
            call    drv_isr_enter

            call    fd_finish_rw$       ; read result phase; cf = error
            jr      c,fdi_err$
            ld      hl,(fd_io_buf$)
            ld      a,(fd_io_ssh$)
            add     a,h
            ld      h,a                 ; advance buffer by one sector (ssh*256)
            ld      (fd_io_buf$),hl
            ld      hl,(fd_io_lba$)
            inc     hl
            ld      (fd_io_lba$),hl
            ld      a,(fd_io_left$)
            dec     a
            ld      (fd_io_left$),a
            jr      z,fdi_done$
            call    fd_dma_start_sector$ ; arm the next sector
            jr      fdi_exit$
fdi_err$:
            ld      a,#1
            ld      (fd_io_err$),a
fdi_done$:
            ld      ix,(fd_io_event$)
            call    drv_signal_done     ; unblock the waiting thread
fdi_exit$:
            jp      drv_isr_exit

fd_ioctl::
            jp      drv_ioctl_pos24

            ;; de = sector lba -> c = cyl, b = head (0/1), a = sector (1-based).
            ;; uses the unit's sectors/track (l = spt, h = 2*spt per cylinder).
fd_lba_to_chs$:
            ld      a,(fd_io_spt$)
            ld      l,a
            add     a,a
            ld      h,a                 ; h = sectors per cylinder (2 heads)
            ld      c,#0
fdc_cyl$:
            ld      a,d
            or      a
            jr      nz,fdc_subcyl$
            ld      a,e
            cp      h
            jr      c,fdc_head$
fdc_subcyl$:
            ld      a,e
            sub     h
            ld      e,a
            jr      nc,fdc_noborrow$
            dec     d
fdc_noborrow$:
            inc     c
            jr      fdc_cyl$
fdc_head$:
            ld      b,#0
            ld      a,e
            cp      l
            jr      c,fdc_sector$
            sub     l
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

fd_sense_dev$:
            push    hl
            ld      a,#FD_CMD_SENSE_DRIVE
            call    fd_write_data$
            jr      c,fdsd_fail$
            ld      b,#0
            call    fd_get_hu$
            call    fd_write_data$
            jr      c,fdsd_fail$
            call    fd_read_data$
            pop     hl
            or      a
            ret
fdsd_fail$:
            pop     hl
            scf
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

fd_write_data$:
            push    af                  ; save the command byte: fd_wait_phase$
            call    fd_wait_cmd_ready$  ; clobbers a/bc/e (it reuses e for the
            jr      c,fdwd_to$          ; phase mask), so the stack is the only
            pop     af                  ; register-safe place to hold it across
            out     (FD_PORT_DATA),a    ; the ready wait.
            or      a
            ret
fdwd_to$:
            pop     af
            scf
            ret

fd_read_data$:
            call    fd_wait_res_ready$
            ret     c
            in      a,(FD_PORT_DATA)
            or      a
            ret

fd_wait_cmd_ready$:
            ld      e,#FD_MSR_CMD_READY
            jr      fd_wait_phase$

fd_wait_res_ready$:
            ld      e,#FD_MSR_RES_READY
fd_wait_phase$:
            ld      bc,#FD_POLL_TIMEOUT
fdw_phase$:
            in      a,(FD_PORT_MSR)
            and     #FD_MSR_PHASE_MASK
            cp      e
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,fdw_phase$
            scf
            ret

fd_dev_drv::
            .dw     hd_dev_drv
            .dw     0x0000
            .dw     fd_init
            .dw     fd_open
            .dw     drv_close_nop
            .dw     fd_read
            .dw     fd_write
            .dw     fd_ioctl

fd_dev0::
fd_dev0$:
            .dw     0x0000
            .db     'f','d','0',0,0,0
            .db     0x00
            .db     0x00
            .ds     DEV_DATA_SIZE-1
            .dw     fd_dev_drv

fd_dev1::
fd_dev1$:
            .dw     0x0000
            .db     'f','d','1',0,0,0
            .db     0x00
            .db     0x01
            .ds     DEV_DATA_SIZE-1
            .dw     fd_dev_drv

fd_dev2::
fd_dev2$:
            .dw     0x0000
            .db     'f','d','2',0,0,0
            .db     0x00
            .db     0x02
            .ds     DEV_DATA_SIZE-1
            .dw     fd_dev_drv

fd_dev3::
fd_dev3$:
            .dw     0x0000
            .db     'f','d','3',0,0,0
            .db     0x00
            .db     0x03
            .ds     DEV_DATA_SIZE-1
            .dw     fd_dev_drv

            ;; --- interrupt-driven transfer state -----------------------------
fd_io_dev$:
            .dw     0x0000              ; device being transferred
fd_io_buf$:
            .dw     0x0000              ; current dma buffer address
fd_io_lba$:
            .dw     0x0000              ; current sector (lba)
fd_io_event$:
            .dw     0x0000              ; completion event to signal
fd_io_left$:
            .db     0x00                ; sectors remaining
fd_io_err$:
            .db     0x00                ; non-zero after a failed transfer
fd_io_sec$:
            .db     0x00                ; current sector number (1-based)
fd_io_head$:
            .db     0x00                ; current head
fd_io_cyl$:
            .db     0x00                ; current cylinder
fd_io_cur_cyl$:
            .db     0x00                ; last cylinder actually seeked to
fd_io_dir$:
            .db     0x00                ; 0 = read (b->a), 1 = write (a->b)
fd_io_spt$:
            .db     0x00                ; sectors per track (this transfer)
fd_io_n$:
            .db     0x00                ; fdc sector-size code (this transfer)
fd_io_ssh$:
            .db     0x00                ; sector size >> 8 (this transfer)

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
            .globl  drv_err
            .globl  hd_init
            .globl  hd_dev0
            .globl  drv_signal_done
            .globl  drv_zero_ok_bc_ix
            .globl  drv_isr_enter
            .globl  drv_isr_exit
            .globl  _ir_set
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  ir_refcnt
            .globl  _hd_isr
            .globl  __sys_nvram_cache

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= hd_init()
            ;; ----------------------------------------------------------------
            ;; driver-level sasi/xebec setup, run once at kernel start. reads
            ;; the sda drive type from nvram byte 2 and sends the matching Xebec
            ;; Initialize Drive Characteristics (cyl/head geometry) so the
            ;; controller can read a non-default disk. type 0 = no drive.
            ;; ----------------------------------------------------------------
hd_init::
            ;; reserve one IM2 slot for DMA end-of-block completion. the DMA
            ;; itself supplies the vector byte; the handler is installed once
            ;; here and each transfer re-programs the dma to emit HD_DMA_VEC.
            ld      a,#HD_DMA_VEC
            ld      de,#_hd_isr
            call    _ir_set
            ld      a,(__sys_nvram_cache + HD_NVRAM_TYPE_BYTE)
            and     #HD_NVRAM_SDA_MASK
            rlca
            rlca
            and     #0x03               ; a = sda type index (0..3)
            or      a
            jr      z,hdi_ok$           ; 0 = no drive, nothing to do
            ;; hl = hd_geom_table$ + (type-1) * 8
            dec     a
            ld      l,a
            ld      h,#0
            add     hl,hl
            add     hl,hl
            add     hl,hl
            ld      de,#hd_geom_table$
            add     hl,de               ; hl = geometry payload (survives cmds)
            call    hd_begin_session$
            jr      c,hdi_end$
            ld      a,#HD_CMD_INITDRV
            call    hd_write_cmd_byte$
            jr      c,hdi_end$
            xor     a
            call    hd_write_cmd_byte$  ; lun | lba[20:16]
            jr      c,hdi_end$
            xor     a
            call    hd_write_cmd_byte$  ; lba[15:8]
            jr      c,hdi_end$
            xor     a
            call    hd_write_cmd_byte$  ; lba[7:0]
            jr      c,hdi_end$
            xor     a
            call    hd_write_cmd_byte$  ; length
            jr      c,hdi_end$
            xor     a
            call    hd_write_cmd_byte$  ; control
            jr      c,hdi_end$
            ld      d,#HD_XEBEC_CFG_SIZE
hdi_data$:
            ld      a,(hl)
            call    hd_write_data_byte$
            jr      c,hdi_end$
            inc     hl
            dec     d
            jr      nz,hdi_data$
            call    hd_finish_cmd$
hdi_end$:
            call    hd_end_session$
hdi_ok$:
            ld      hl,#DRV_OK
            ret

            ;; pre-encoded 8-byte Initialize Drive Characteristics payloads
            ;; (cyl_hi, cyl_lo, heads, rwc_hi, rwc_lo, wpc_hi, wpc_lo, ecc),
            ;; borrowed from the rom hd_geom_table$:
hd_geom_table$:
            .db     0x00,0x99,0x04,0x00,0x00,0x00,0x00,0x0b ; 1: ST-506 153/4
            .db     0x01,0x32,0x04,0x00,0x80,0x00,0x40,0x0b ; 2: ST-412 306/4
            .db     0x02,0x67,0x04,0x00,0x00,0x01,0x2c,0x0b ; 3: ST-225 615/4

hd_open::
            jp      drv_open_pos0

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= hd_read (<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; <hl> rc <= hd_write(<hl> dev, <de> buf, <bc> count, <ix> event)
            ;; ----------------------------------------------------------------
            ;; bulk dma transfer: one SASI READ(6)/WRITE(6) for the whole
            ;; request, with the data phase streamed by the z80 dma (read =
            ;; sasi->ram, write = ram->sasi). once the programmed byte count
            ;; reaches zero the DMA raises HD_DMA_VEC; the ISR consumes the
            ;; final Xebec response phase, drops the session, clears busy and
            ;; signals the caller's completion event.
            ;; ----------------------------------------------------------------
hd_read::
            call    drv_zero_ok_bc_ix
            ret     z
            xor     a                   ; dir = 0 (read)
            jr      hd_xfer$

hd_write::
            call    drv_zero_ok_bc_ix
            ret     z
            ld      a,#1                ; dir = 1 (write)

hd_xfer$:                               ; a=dir, hl=dev, de=buf, bc=count, ix=event
            ld      (hd_io_dir$),a
            ld      (hd_io_dev$),hl
            ld      (hd_io_ptr$),de
            push    ix
            pop     de
            ld      (hd_io_event$),de
            xor     a
            ld      (hd_io_err$),a
            call    hd_claim_dev$
            jr      nc,hdx_claimed$
            ld      hl,#DRV_ERR
            ret
hdx_claimed$:
            push    hl
            call    drv_prep_rw256      ; de = lba (preserves bc)
            ld      a,h
            or      l
            jr      z,hdx_prep_ok$
            pop     hl
            call    hd_release_dev$
            ret
hdx_prep_ok$:
            pop     hl
            ;; reserve the current byte cursor immediately on acceptance so a
            ;; later caller cannot reuse the same range while the DMA engine is
            ;; still moving the active request.
            push    de
            push    bc
hdx_adv$:
            push    bc
            ld      hl,(hd_io_dev$)
            call    drv_advance_pos256
            pop     bc
            djnz    hdx_adv$
            pop     bc
            pop     de
            ld      a,b
            ld      (hd_xfer_count$),a
            call    _ir_disable
            call    hd_begin_session$
            jr      c,hdx_begin_fail$
            ;; arm the DMA before the command is issued so the first DRQ edge
            ;; of the data phase can be consumed immediately.
            ld      hl,(hd_io_ptr$)
            ld      a,(hd_xfer_count$)
            dec     a
            ld      d,a                 ; length-1 high = count - 1
            ld      e,#0xff             ; length-1 low  = 255  (count*256 - 1)
            call    hd_dma_setup$
            ld      a,(hd_xfer_count$)
            ld      b,a
            ld      a,(hd_io_dir$)
            or      a
            ld      a,#HD_CMD_READ6
            jr      z,hdx_cmd$
            ld      a,#HD_CMD_WRITE6
hdx_cmd$:
            call    hd_issue_rw6$       ; a = cmd, b = block count, de = lba
            jr      c,hdx_cmd_fail$
            call    _ir_enable
            ld      hl,#DRV_OK
            ret
hdx_begin_fail$:
            call    hd_end_session$
            call    _ir_enable
            ld      hl,(hd_io_dev$)
            call    hd_release_dev$
            jp      drv_err
hdx_cmd_fail$:
            call    hd_dma_abort$
            call    hd_end_session$
            call    _ir_enable
            ld      hl,(hd_io_dev$)
            call    hd_release_dev$
            jp      drv_err

            ;; ----------------------------------------------------------------
            ;; hd_dma_setup$(<hl> buffer, <de> length-1)
            ;; ----------------------------------------------------------------
            ;; programs the z80 dma to move the data phase between the sasi data
            ;; port (port b, fixed) and the buffer (port a, increment). the
            ;; direction (wr0 0x79 read = b->a, 0x7d write = a->b) follows
            ;; hd_io_dir$. in addition to the boot-rom compatible transfer
            ;; fields, the setup enables dma end-of-block interrupts and loads
            ;; the fixed HD_DMA_VEC vector byte that the ISR is installed on.
            ;; ----------------------------------------------------------------
hd_dma_setup$:
            ld      a,#0x05
            out     (HD_PORT_DMA),a     ; reset dma
            ld      a,#0xcf
            out     (HD_PORT_DMA),a     ; continuous mode pre-load
            ld      a,#0x38
            out     (HD_PORT_DMA),a     ; wr0 base 7: interrupt control follows
            ld      a,#0x20
            out     (HD_PORT_DMA),a     ; enable dma end-of-block interrupt
            ld      a,#0x48
            out     (HD_PORT_DMA),a     ; wr0 base 9: interrupt vector follows
            ld      a,#HD_DMA_VEC
            out     (HD_PORT_DMA),a
            ld      a,(hd_io_dir$)
            or      a
            ld      a,#0x79             ; read: sasi (port b) -> mem (port a)
            jr      z,hdds_wr0$
            ld      a,#0x7d             ; write: mem (port a) -> sasi (port b)
hdds_wr0$:
            out     (HD_PORT_DMA),a     ; wr0: direction, addr+len follow
            ld      a,l
            out     (HD_PORT_DMA),a     ; buffer low
            ld      a,h
            out     (HD_PORT_DMA),a     ; buffer high
            ld      a,e
            out     (HD_PORT_DMA),a     ; length low
            ld      a,d
            out     (HD_PORT_DMA),a     ; length high
            ld      hl,#hd_dma_cfg$
            ld      b,#10
            ld      c,#HD_PORT_DMA
            otir
            ret

hd_dma_cfg$:
            .db     0x14,0x28,0x95,HD_PORT_DATA,0x00,0x8a,0xcf,0x01,0xcf,0x87

hd_dma_abort$:
            ld      a,#0x05
            out     (HD_PORT_DMA),a
            ret

hd_ioctl::
            jp      drv_ioctl_pos24

            ;; ----------------------------------------------------------------
            ;; hd_claim_dev$(<hl> dev)
            ;; ----------------------------------------------------------------
            ;; claims the single hd instance for one in-flight async request.
            ;; cf=1 means the device was already busy and the caller must fail.
            ;; ----------------------------------------------------------------
hd_claim_dev$:
            call    _ir_disable
            push    hl
            ld      de,#DEV_FLAGS
            add     hl,de
            bit     1,(hl)
            jr      nz,hdc_busy$
            set     1,(hl)
            res     2,(hl)
            pop     hl
            call    _ir_enable
            or      a
            ret
hdc_busy$:
            pop     hl
            call    _ir_enable
            scf
            ret

hd_release_dev$:
            call    _ir_disable
            push    hl
            ld      de,#DEV_FLAGS
            add     hl,de
            res     1,(hl)
            pop     hl
            call    _ir_enable
            ret

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
            ld      e,#HD_STATUS_CMD
            jr      hd_wait_phase$

hd_wait_data_in$:
            ld      e,#HD_STATUS_DATA_IN
            jr      hd_wait_phase$

hd_wait_data_out$:
            ld      e,#HD_STATUS_DATA_OUT
            jr      hd_wait_phase$

hd_wait_resp_phase$:
            ld      e,#HD_STATUS_RESP
hd_wait_phase$:
            ld      bc,#HD_POLL_TIMEOUT
hdw_phase$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
            cp      e
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_phase$
            jr      hdw_fail$

            ;; ----------------------------------------------------------------
            ;; _hd_isr() -- im 2 dma end-of-block handler for SASI transfers
            ;; ----------------------------------------------------------------
            ;; the dma interrupt only means the programmed byte count moved.
            ;; this handler still has to wait for the xebec response phase,
            ;; read its status/message bytes, drop the session and then signal
            ;; the waiting event. the ir bracket is held because drv_signal_done
            ;; eventually walks kernel lists through evt_set.
            ;; ----------------------------------------------------------------
_hd_isr::
            call    drv_isr_enter

            call    hd_finish_cmd$
            jr      c,hdi_err$
            xor     a
            ld      (hd_io_err$),a
            jr      hdi_done$
hdi_err$:
            ld      a,#1
            ld      (hd_io_err$),a
hdi_done$:
            call    hd_end_session$
            ld      hl,(hd_io_dev$)
            ld      de,#DEV_FLAGS
            add     hl,de
            res     1,(hl)
            ld      a,(hd_io_err$)
            or      a
            jr      z,hdi_noerr$
            set     2,(hl)
            jr      hdi_signal$
hdi_noerr$:
            res     2,(hl)
hdi_signal$:
            ld      ix,(hd_io_event$)
            call    drv_signal_done
            jp      drv_isr_exit

hd_dev_drv::
            .dw     0x0000              ; end of built-in driver chain
            .dw     0x0000
            .dw     hd_init
            .dw     hd_open
            .dw     drv_close_nop
            .dw     hd_read
            .dw     hd_write
            .dw     hd_ioctl

hd_dev0::
hd_dev0$:
            .dw     0x0000
            .db     's','d','a',0,0,0
            .db     0x00
            .db     0x00
            .ds     DEV_DATA_SIZE-1
            .dw     hd_dev_drv

hd_io_ptr$:
            .dw     0x0000

hd_io_dev$:
            .dw     0x0000

hd_io_event$:
            .dw     0x0000

hd_xfer_count$:
            .db     0x00

hd_io_dir$:
            .db     0x00                ; 0 = read (b->a), 1 = write (a->b)

hd_io_err$:
            .db     0x00

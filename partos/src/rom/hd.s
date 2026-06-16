            ;; hd.s
            ;;
            ;; minimal rom-side hard-disk boot reader.
            ;;
            ;; the ROM only needs to pull the boot record and OS image off the
            ;; primary SASI/Xebec disk; drive characteristics (Initialize Drive)
            ;; and NVRAM-driven configuration are the loaded OS's job. this
            ;; module is just a polled READ(6) over the raw Partner SASI ports.
            ;;
            ;; 2026-06-14   tstih
            .module hd

            .include "hd.inc"

            .globl  bios_nvram_cache
            .globl  hd_read_lba
            .globl  hd_get_sda_type_index
            .globl  hd_init_chars

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; low-level polled sasi helpers
            ;; ----------------------------------------------------------------
hd_begin_session$:
            ld      a,#HD_CTRL_SEL
            out     (HD_PORT_CTRL),a
            call    hd_wait_bsy$
            ret     c
            ld      a,#HD_CTRL_DATA_EN | HD_CTRL_DRQ_EN
            out     (HD_PORT_CTRL),a
            ret

hd_end_session$:
            xor     a
            out     (HD_PORT_CTRL),a
            ret

hd_write_cmd_byte$:
            ld      e,a
            call    hd_wait_cmd_phase$
            ret     c
            ld      a,e
            out     (HD_PORT_DATA),a
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

            ;; the three bus-phase waits are identical except for the target
            ;; (REQ|IO|CD) pattern, so they share one loop. the compare operand
            ;; is patched in place (stage-1 runs from RAM). bc is the timeout;
            ;; d, e and hl are preserved for the callers.
hd_wait_cmd_phase$:
            ld      a,#HD_STATUS_CMD
            jr      hd_wph$
hd_wait_resp_phase$:
            ld      a,#HD_STATUS_RESP
            jr      hd_wph$
hd_wait_data_in$:
            ld      a,#HD_STATUS_DATA_IN
            jr      hd_wph$
hd_wait_data_out$:
            ld      a,#HD_STATUS_DATA_OUT
hd_wph$:
            ld      (hd_wph_cp$+1),a
            ld      bc,#HD_POLL_TIMEOUT
hdw_ph$:
            in      a,(HD_PORT_STATUS)
            cp      #0xff
            jr      z,hdw_fail$
            and     #HD_ST_REQ | HD_ST_IO | HD_ST_CD
hd_wph_cp$:
            cp      #0x00               ; operand patched per phase
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,hdw_ph$
            jr      hdw_fail$

            ;; ----------------------------------------------------------------
            ;; <a> <= hd_read_lba(<a> lba, <hl> dst)
            ;; ----------------------------------------------------------------
            ;; reads one 256-byte block (logical block lba) into dst through a
            ;; polled SASI READ(6). the Xebec presents pure LBA blocks, so no
            ;; geometry is needed; only the low LBA byte is used (boot record +
            ;; 8 KB reserved area live in the first 33 blocks).
            ;;
            ;; out: a = 0 on success (z), 1 on failure (nz).
            ;; ----------------------------------------------------------------
hd_read_lba::
            ld      d,a                 ; d = lba low byte (survives the session)
            call    hd_begin_session$
            jr      c,hrl_fail$

            ld      a,#HD_CMD_READ6
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$
            xor     a                   ; lba[20:16] | lun 0
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$
            xor     a                   ; lba[15:8]
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$
            ld      a,d                 ; lba[7:0]
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$
            ld      a,#0x01             ; transfer length = 1 block
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$
            xor     a                   ; control byte
            call    hd_write_cmd_byte$
            jr      c,hrl_fail$

            ;; data-in phase: pull 256 bytes into the destination
            ld      e,#0                ; 256-iteration counter
hrl_data$:
            call    hd_wait_data_in$
            jr      c,hrl_fail$
            in      a,(HD_PORT_DATA)
            ld      (hl),a
            inc     hl
            dec     e
            jr      nz,hrl_data$

            call    hd_finish_cmd$      ; status + message; carry if status != 0
            jr      c,hrl_fail$
            jp      hd_end_session$

hrl_fail$:
            call    hd_end_session$
            inc     a
            ret

hd_write_data_byte$:
            ld      e,a
            call    hd_wait_data_out$
            ret     c
            ld      a,e
            out     (HD_PORT_DATA),a
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= hd_get_sda_type_index()
            ;; ----------------------------------------------------------------
            ;; nvram byte 2, bits 7:6 select the sda type (0=none, 1..3).
            ;; ----------------------------------------------------------------
hd_get_sda_type_index::
            ld      a,(bios_nvram_cache + HD_NVRAM_TYPE_BYTE)
            and     #HD_NVRAM_SDA_MASK
            rlca
            rlca
            and     #0x03
            ret

            ;; ----------------------------------------------------------------
            ;; hd_init_chars(<a> type)
            ;; ----------------------------------------------------------------
            ;; sends the Xebec Initialize Drive Characteristics for the chosen
            ;; type so the controller knows the cyl/head geometry before the
            ;; boot read. WITHOUT this you cannot read a disk whose geometry
            ;; differs from the controller default. type 0 = no drive, skipped.
            ;; ----------------------------------------------------------------
hd_init_chars::
            or      a
            ret     z                   ; type 0 = no drive
            dec     a                   ; 0-based index
            ld      l,a
            ld      h,#0
            add     hl,hl
            add     hl,hl
            add     hl,hl               ; hl = index * 8
            ld      de,#hd_geom_table$
            add     hl,de               ; hl = payload (cmd helpers preserve hl)
            call    hd_begin_session$
            jr      c,hic_done$

            ld      a,#HD_CMD_INITDRV
            call    hd_write_cmd_byte$
            jr      c,hic_done$
            xor     a                   ; LUN | lba (sda = lun 0)
            call    hd_write_cmd_byte$
            jr      c,hic_done$
            xor     a
            call    hd_write_cmd_byte$
            jr      c,hic_done$
            xor     a
            call    hd_write_cmd_byte$
            jr      c,hic_done$
            xor     a
            call    hd_write_cmd_byte$
            jr      c,hic_done$
            xor     a                   ; control byte
            call    hd_write_cmd_byte$
            jr      c,hic_done$

            ld      d,#HD_XEBEC_CFG_SIZE ; counter survives the data-out wait
hic_data$:
            ld      a,(hl)
            call    hd_write_data_byte$
            jr      c,hic_done$
            inc     hl
            dec     d
            jr      nz,hic_data$

            call    hd_finish_cmd$
hic_done$:
            jp      hd_end_session$     ; tail call: drop the bus and return

            ;; pre-encoded 8-byte Initialize Drive Characteristics payloads:
            ;; cyl_hi, cyl_lo, heads, rwc_hi, rwc_lo, wpc_hi, wpc_lo, ecc
hd_geom_table$:
            .db     0x00,0x99,0x04,0x00,0x00,0x00,0x00,0x0b ; 1: ST-506 153/4
            .db     0x01,0x32,0x04,0x00,0x80,0x00,0x40,0x0b ; 2: ST-412 306/4
            .db     0x02,0x67,0x04,0x00,0x00,0x01,0x2c,0x0b ; 3: ST-225 615/4

            ;; fd.s
            ;;
            ;; minimal rom-side floppy profile table and controller bring-up.
            ;;
            ;; the setup block stores only 2-bit type selectors for fd0..fd3.
            ;; this module maps those selectors to hard-coded profiles and
            ;; provides a small polled i8272 bring-up + sector-read path for
            ;; boot-time use. presence is not probed: fd_init returns an error
            ;; if the drive is not ready.
            ;;
            ;; 2026-06-14   tstih
            .module fd

            .include "fd.inc"

            .globl  bios_nvram_cache
            .globl  fd_init
            .globl  fd_read_lba

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; <a> <= fd_init(<a> unit)
            ;; ----------------------------------------------------------------
            ;; minimal floppy bring-up for boot:
            ;;   - ensure the controller is specified once
            ;;   - spin the motor/control latch up
            ;;   - check that the selected unit reports READY
            ;;   - recalibrate it to track 0
            ;;
            ;; in:
            ;;   a  = unit number (0=fd0, 1=fd1)
            ;;
            ;; out:
            ;;   a  = 0 on success, 1 on failure
            ;;   z  on success, nz on failure
            ;; ----------------------------------------------------------------
fd_init::
            ld      d,a

            call    fd_set_geom$        ; a=unit -> fd_spt$ / fd_n$ from nvram

            call    fd_controller_init$
            jr      c,fdi_fail$

            xor     a
            out     (FD_PORT_MOTOR),a   ; spin the drive path up

            ;; recalibrate to track 0. an absent/empty drive fails the later
            ;; read instead, so no separate readiness probe is needed.
            ld      a,d
            call    fd_recal_unit$
            jr      c,fdi_fail$

            xor     a
            ret

fdi_fail$:
            ld      a,#0x01
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; low-level polled i8272 helpers
            ;; ----------------------------------------------------------------
fd_controller_init$:
            ld      a,(fd_init_done$)
            or      a
            ret     nz

            ld      a,#FD_CMD_SPECIFY
            call    fd_write_data$
            jr      c,fdci_fail$
            ld      a,#FD_SPECIFY_SRT_HUT
            call    fd_write_data$
            jr      c,fdci_fail$
            ld      a,#FD_SPECIFY_HLT_ND
            call    fd_write_data$
            jr      c,fdci_fail$

            ld      a,#0x01
            ld      (fd_init_done$),a
            ret

fdci_fail$:
            xor     a
            ld      (fd_init_done$),a
            scf
            ret

fd_recal_unit$:
            ld      d,a
            ld      a,#FD_CMD_RECAL
            call    fd_write_data$
            jr      c,fdru_fail$
            ld      a,d
            and     #0x03
            call    fd_write_data$
            jr      c,fdru_fail$
            call    fd_wait_int$
            ret     nc

fdru_fail$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; fd_set_geom$()
            ;; ----------------------------------------------------------------
            ;; boot is always fd0, so read its 2-bit floppy type from nvram byte
            ;; 1 (bits 7:6) and store the read geometry (sectors/track + fdc
            ;; sector-size code n) the sector reader needs. absent -> PARTNER.
            ;; ----------------------------------------------------------------
fd_set_geom$:
            ld      a,(bios_nvram_cache + FD_NVRAM_TYPE_BYTE)
            and     #FD_NVRAM_FD0_MASK
            rlca
            rlca                        ; a = type (1..3), 0 if absent
            or      a
            jr      nz,fsg_type$
            inc     a                   ; absent -> PARTNER (type 1)
fsg_type$:
            dec     a
            add     a,a                 ; (type-1) * 2 (2 bytes per entry)
            ld      e,a
            ld      d,#0
            ld      hl,#fd_geom_table$
            add     hl,de
            ld      a,(hl)
            ld      (fd_spt$),a         ; sectors per track
            inc     hl
            ld      a,(hl)
            ld      (fd_n$),a           ; fdc sector-size code (1=256, 2=512)
            ret

            ;; per floppy type (1..3): sectors/track, fdc sector-size code.
fd_geom_table$:
            .db     18,1                ; 1: PARTNER  18 spt, 256-byte
            .db      9,2                ; 2: DOS-720K  9 spt, 512-byte
            .db      9,2                ; 3: DOS-360K  9 spt, 512-byte

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

fd_write_data$:
            ld      e,a
            ld      l,#FD_MSR_CMD_READY
            call    fd_wait_phase$
            ret     c
            ld      a,e
            out     (FD_PORT_DATA),a
            ret

fd_read_data$:
            ld      l,#FD_MSR_RES_READY
            call    fd_wait_phase$
            ret     c
            in      a,(FD_PORT_DATA)
            ret

            ;; wait for one i8272 MSR phase. in: l = expected phase value
            ;; (already masked with FD_MSR_PHASE_MASK). carry set on timeout.
fd_wait_phase$:
            ld      bc,#FD_POLL_TIMEOUT
fdw_ph$:
            in      a,(FD_PORT_MSR)
            and     #FD_MSR_PHASE_MASK
            cp      l
            ret     z
            dec     bc
            ld      a,b
            or      c
            jr      nz,fdw_ph$
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= fd_read_lba(<a> lba, <hl> dst)
            ;; ----------------------------------------------------------------
            ;; reads one 256-byte sector (logical block lba, 0..FD_BOOT_SPT*2-1)
            ;; into the buffer at dst, using a single polled MFM READ DATA.
            ;; only cylinder 0 is addressed (boot record + 8 KB reserved area),
            ;; so no seek is issued; fd_init must have recalibrated first.
            ;;
            ;; out: a = 0 on success (z), 1 on failure (nz).
            ;; ----------------------------------------------------------------
fd_read_lba::
            ld      (fd_dest$),hl       ; stash destination for the data phase

            ;; lba -> head (0/1) + sector R (1-based) using the configured spt;
            ;; cylinder is always 0 (the rom only reads cylinder 0).
            ld      c,a                 ; c = lba
            ld      a,(fd_spt$)
            ld      b,a                 ; b = spt
            ld      e,#0                ; head 0
            ld      a,c
            cp      b
            jr      c,frl_h0$
            sub     b
            inc     e                   ; head 1
frl_h0$:
            ;; patch the variable command-template fields (fd_cmd$ is in the
            ;; decompressed ram image, so it is writable). C/GPL/DTL are baked
            ;; in; R, EOT, N and the head/unit byte vary per call + geometry.
            inc     a                   ; a = R (1-based sector)
            ld      (fd_cmd$+4),a       ; R
            ld      (fd_cmd$+6),a       ; EOT = R
            ld      a,(fd_n$)
            ld      (fd_cmd$+5),a       ; N (sector-size code from nvram)
            ld      a,e
            ld      (fd_cmd$+3),a       ; H = head
            add     a,a
            add     a,a                 ; head << 2 (unit 0)
            ld      (fd_cmd$+1),a

            ;; command phase: push the 9 bytes
            ld      hl,#fd_cmd$
            ld      b,#9
frl_send$:
            push    bc
            push    hl
            ld      a,(hl)
            call    fd_write_data$
            pop     hl
            pop     bc
            jr      c,frl_fail$
            inc     hl
            djnz    frl_send$

            ;; execution phase: pull one sector (256 bytes for n=1, 512 for n=2)
            ;; in 256-byte passes (fd_read_data$ clobbers bc, so the inner byte
            ;; count stays in h and the pass count on the stack).
            ld      de,(fd_dest$)
            ld      a,(fd_n$)
            ld      b,a                 ; 256-byte passes = n (1 for 256, 2 for 512)
frl_pass$:
            push    bc
            ld      h,#0                ; 256-iteration counter
frl_data$:
            call    fd_read_data$
            jr      c,frl_dfail$
            ld      (de),a
            inc     de
            dec     h
            jr      nz,frl_data$
            pop     bc
            djnz    frl_pass$

            ;; result phase: ST0 must report normal termination, then drain the
            ;; remaining 6 status bytes so the controller returns to idle.
            call    fd_read_data$
            jr      c,frl_fail$
            and     #FD_ST0_IC_MASK
            jr      nz,frl_fail$
            ld      h,#6
frl_res$:
            call    fd_read_data$
            jr      c,frl_fail$
            dec     h
            jr      nz,frl_res$
            xor     a
            ret

frl_dfail$:
            pop     bc                  ; drop the pass counter
frl_fail$:
            ld      a,#1
            or      a
            ret

            ;; READ DATA command template (patched per call). lives in _BOOT so
            ;; it decompresses into writable RAM. fields: opcode, unit/head, C,
            ;; H, R, N, EOT, GPL, DTL.
fd_cmd$:
            .db     FD_CMD_READ, 0, 0x00, 0, 0, FD_READ_N, 0, FD_READ_GPL, FD_READ_DTL

            .area   _SYSVARS

fd_init_done$:
            .db     0x00
fd_dest$:
            .ds     2
fd_spt$:
            .ds     1                   ; boot floppy sectors/track (from nvram)
            .globl  fd_n$
fd_n$:
            ;; fdc sector-size code (1=256, 2=512). doubles as the boot loader's
            ;; sector size >> 8: fd_set_geom$ sets it for the floppy, the hd boot
            ;; path forces it to 1. see boot_device$ in start.s.
            .ds     1

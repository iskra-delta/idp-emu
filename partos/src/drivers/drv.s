            ;; drv.s
            ;;
            ;; common bios driver helpers
            ;;
            ;; 2026-06-13   tstih
            .module drv

            .include "dev.inc"
            .include "drv.inc"

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; drv_reset_dev(<hl> *dev, <de> *driver)
            ;; ----------------------------------------------------------------
            ;; resets the mutable runtime fields of a static device instance:
            ;; next, flags, private data and owning driver pointer. the name
            ;; bytes stay as-is so each driver can keep its static prefix.
            ;;
            ;; input(s):
            ;;  hl  ... dev_t*
            ;;  de  ... dev_drv_t*
            ;; output(s):
            ;;  hl  ... original dev_t*
            ;; destroys:
            ;;  a, bc
            ;; ----------------------------------------------------------------
drv_reset_dev::
            push    hl
            xor     a
            ld      (hl),a              ; next low
            inc     hl
            ld      (hl),a              ; next high
            ld      bc,#DEV_FLAGS-1
            add     hl,bc               ; hl = dev->flags
            ld      (hl),a              ; flags = 0
            inc     hl                  ; hl = dev->data
            ld      b,#16
drd_zero$:
            ld      (hl),a
            inc     hl
            djnz    drd_zero$
            ld      (hl),e              ; driver low
            inc     hl
            ld      (hl),d              ; driver high
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; generic stub entry points for unfinished drivers
            ;; ----------------------------------------------------------------
drv_open_ok::
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; drv_open_pos0(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; resets the shared 24-bit byte cursor in dev.data[] to offset 0.
            ;; drivers that behave like flat files can use this as open().
            ;; ----------------------------------------------------------------
drv_open_pos0::
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            pop     hl
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; <de> lba <= drv_prep_rw256(<hl> *dev, <bc> count)
            ;; ----------------------------------------------------------------
            ;; validates a 256-byte aligned flat-file transfer:
            ;;   - count must be a multiple of 256 bytes (c == 0)
            ;;   - current byte offset must also be 256-byte aligned
            ;; on success de receives the current 16-bit block/lba index
            ;; (byte_offset >> 8). zero-length requests are treated as success
            ;; and leave de unspecified.
            ;; ----------------------------------------------------------------
drv_prep_rw256::
            ld      a,c
            or      a
            jr      z,drp_chkpos$
            ld      hl,#DRV_ERR
            ret
drp_chkpos$:
            push    bc
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(hl)
            or      a
            jr      z,drp_load$
            pop     bc
            ld      hl,#DRV_ERR
            ret
drp_load$:
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     bc
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; drv_advance_pos256(<hl> *dev)
            ;; ----------------------------------------------------------------
            ;; advances the shared 24-bit byte cursor by one 256-byte block.
            ;; ----------------------------------------------------------------
drv_advance_pos256::
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS1
            add     hl,bc
            inc     (hl)
            jr      nz,dap_done$
            inc     hl
            inc     (hl)
dap_done$:
            pop     hl
            ret

drv_close_nop::
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> rc <= drv_ioctl_pos24(<hl> *dev, <de> *pos, <bc> cmd)
            ;; ----------------------------------------------------------------
            ;; shared byte-position ioctl used by flat-file style block drivers.
            ;; de points to a 24-bit little-endian byte offset buffer.
            ;; ----------------------------------------------------------------
drv_ioctl_pos24::
            ld      a,b
            or      a
            jr      z,dip_cmd$
            ld      hl,#DRV_ERR
            ret
dip_cmd$:
            ld      a,c
            cp      #IOCTL_GETPOS
            jr      z,dip_get$
            cp      #IOCTL_SETPOS
            jr      z,dip_set$
            ld      hl,#DRV_ERR
            ret
dip_get$:
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            ld      a,(hl)
            ld      (de),a
            pop     hl
            ld      hl,#DRV_OK
            ret
dip_set$:
            push    hl
            ld      bc,#DEV_DATA + DRV_DATA_POS0
            add     hl,bc
            ld      a,(de)
            ld      (hl),a
            inc     hl
            inc     de
            ld      a,(de)
            ld      (hl),a
            inc     hl
            inc     de
            ld      a,(de)
            ld      (hl),a
            pop     hl
            ld      hl,#DRV_OK
            ret

drv_read_unsupported::
drv_write_unsupported::
drv_ioctl_unsupported::
            ld      hl,#DRV_ERR
            ret

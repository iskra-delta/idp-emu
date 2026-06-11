            ;; dev.s
            ;;
            ;; device list management. the device list is a standard list
            ;; (next first, see list.s); driver probe results are appended
            ;; to dev_list with append_list.
            ;;
            ;; 2026-06-11   tstih
            .module dev

            .include "dev.inc"

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; dev_init()
            ;; ----------------------------------------------------------------
            ;; clears the global device list. call once at boot, before the
            ;; driver probe pass.
            ;;
            ;; destroys:
            ;;  hl  ... 0
            ;; ----------------------------------------------------------------
dev_init::
            ld      hl,#0x0000
            ld      (dev_list),hl
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> *drv, <de> *dev <= find_dev_drv(<hl> *name)
            ;; ----------------------------------------------------------------
            ;; finds a device by name in the global device list and returns
            ;; its driver through the dev_t back pointer. the matched device
            ;; instance is returned in de, ready for the driver open(dev)
            ;; call. device names are zero terminated (dev_t.reserved
            ;; guarantees termination of a full 8 character name).
            ;;
            ;; input(s):
            ;;  hl  ... pointer to zero terminated device name
            ;; output(s):
            ;;  hl  ... dev_drv_t* of the matched device, 0x0000 if not found
            ;;  de  ... dev_t* of the matched device, 0x0000 if not found
            ;;  flags   ... Z if found, NZ if not found
            ;; destroys:
            ;;  a   ... 0 if found, 0xff if not found
            ;; ----------------------------------------------------------------
find_dev_drv::
            ld      de,(dev_list)       ; de = current dev_t
fdd_loop$:
            ld      a,d
            or      e                   ; end of list?
            jr      z,fdd_fail$
            push    hl                  ; save name pointer
            push    de                  ; save current dev_t
            inc     de
            inc     de                  ; de = dev->name (DEV_NAME = 2)
fdd_cmp$:
            ld      a,(de)              ; device name character
            cp      (hl)                ; compare with wanted character
            jr      nz,fdd_next$        ; mismatch: try next device
            or      a                   ; terminator on both sides?
            jr      z,fdd_found$        ; yes: names match
            inc     de
            inc     hl
            jr      fdd_cmp$
fdd_next$:
            pop     hl                  ; hl = current dev_t
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = current->next (offset 0)
            pop     hl                  ; restore name pointer
            jr      fdd_loop$
fdd_found$:
            pop     de                  ; de = matched dev_t
            pop     hl                  ; drop saved name pointer
            ld      hl,#DEV_DRIVER
            add     hl,de
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = dev->driver
            xor     a                   ; Z = found
            ret
fdd_fail$:
            ld      hl,#0x0000          ; no driver
            ld      de,#0x0000          ; no device
            ld      a,#0xff
            or      a                   ; NZ = not found
            ret

            .area   _SYSVARS
            ;; head of the global device list
dev_list::
            .ds     2

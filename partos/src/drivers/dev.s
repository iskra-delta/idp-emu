            ;; dev.s
            ;;
            ;; device list management for the kernel-owned driver set.
            ;; probe results are appended to the single global device list.
            ;;
            ;; 2026-06-14   tstih
            .module dev

            .include "dev.inc"

            .globl  _list_append
            .globl  ctc_init
            .globl  nvram_init
            .globl  rtc_init
            .globl  avdc_init
            .globl  sio_init
            .globl  pio_init
            .globl  fd_init
            .globl  hd_init

            .globl  ctc_dev_drv
            .globl  ctc_dev
            .globl  sio_dev_drv
            .globl  sio_dev0
            .globl  sio_dev1
            .globl  sio_dev2
            .globl  sio_dev3
            .globl  rtc_dev_drv
            .globl  rtc_dev
            .globl  nvram_dev_drv
            .globl  nvram_dev
            .globl  avdc_dev_drv
            .globl  avdc_dev0
            .globl  pio_dev_drv
            .globl  pio_dev0
            .globl  fd_dev_drv
            .globl  fd_dev0
            .globl  fd_dev1
            .globl  fd_dev2
            .globl  fd_dev3
            .globl  hd_dev_drv
            .globl  hd_dev0

            .globl  __dev_init
            .globl  __dev_init_all
            .globl  __dev_probe_all
            .globl  __drv_register_all
            .globl  __find_dev_drv
            .globl  dev_find_by_name
            .globl  _dev_first
            .globl  _drv_first
            .globl  __sys_model
            .globl  __sys_nvram_cache

            .equ    MODEL_F_GRAPHICS,   0x01
            .equ    FD_NVRAM_TYPE_BYTE, 1
            .equ    HD_NVRAM_TYPE_BYTE, 2
            .equ    HD_NVRAM_SDA_MASK,  0xc0
            .equ    SIO_NVRAM_ATTACH_BYTE, 3

            .area   _CODE

dev_call_de$:
            push    de
            ret

dev_append_hl$:
            ld      a,h
            or      l
            ret     z
            ex      de,hl
            ld      hl,#_dev_first
            jp      _list_append

            ;; ----------------------------------------------------------------
            ;; __dev_init()
            ;; ----------------------------------------------------------------
            ;; clears the global device list. call once before the driver
            ;; probe pass.
            ;; ----------------------------------------------------------------
__dev_init::
            ld      hl,#0x0000
            ld      (_dev_first),hl
            ret

            ;; ----------------------------------------------------------------
            ;; __dev_init_all()
            ;; ----------------------------------------------------------------
            ;; runs every driver's one-time, driver-level init (controller /
            ;; chip setup) once at kernel start. call before __dev_probe_all.
            ;; per-device setup is deferred to each device's open().
            ;; ----------------------------------------------------------------
__dev_init_all::
            ld      hl,(_drv_first)
dia_loop$:
            ld      a,h
            or      l
            ret     z
            push    hl
            ld      de,#DRV_INIT
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      a,d
            or      e
            call    nz,dev_call_de$
            pop     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            jr      dia_loop$

            ;; ----------------------------------------------------------------
            ;; __drv_register_all()
            ;; ----------------------------------------------------------------
            ;; built-in drivers are linked statically through each drv_t.next
            ;; field, so the public registration hook can stay as a no-op while
            ;; _drv_first still exposes an enumerable driver chain to the OS.
            ;; ----------------------------------------------------------------
__drv_register_all::
            ret

            ;; ----------------------------------------------------------------
            ;; __dev_probe_all()
            ;; ----------------------------------------------------------------
            ;; appends the boot-time configured device chains in fixed built-in
            ;; order. simple static devices are published directly here; only
            ;; the multi-unit NVRAM-driven families still need custom builders.
            ;; ----------------------------------------------------------------
__dev_probe_all::
            ld      hl,#ctc_dev
            call    dev_append_hl$

            ld      a,(__sys_nvram_cache + SIO_NVRAM_ATTACH_BYTE)
            ld      d,a
            and     #0xc0
            jr      z,dpa_no_sio0$
            ld      hl,#sio_dev0
            call    dev_append_hl$
dpa_no_sio0$:
            ld      a,d
            and     #0x30
            jr      z,dpa_no_sio1$
            ld      hl,#sio_dev1
            call    dev_append_hl$
dpa_no_sio1$:
            ld      a,d
            and     #0x0c
            jr      z,dpa_no_sio2$
            ld      hl,#sio_dev2
            call    dev_append_hl$
dpa_no_sio2$:
            ld      a,d
            and     #0x03
            jr      z,dpa_no_sio3$
            ld      hl,#sio_dev3
            call    dev_append_hl$
dpa_no_sio3$:

            ld      hl,#pio_dev0
            call    dev_append_hl$

            ld      hl,#rtc_dev
            call    dev_append_hl$

            ld      hl,#nvram_dev
            call    dev_append_hl$

            ld      a,(__sys_model)
            and     #MODEL_F_GRAPHICS
            ld      hl,#0x0000
            jr      z,dpa_no_avdc$
            ld      hl,#avdc_dev0
dpa_no_avdc$:
            call    dev_append_hl$

            ld      a,(__sys_nvram_cache + FD_NVRAM_TYPE_BYTE)
            ld      d,a
            and     #0xc0
            jr      z,dpa_no_fd0$
            ld      hl,#fd_dev0
            call    dev_append_hl$
dpa_no_fd0$:
            ld      a,d
            and     #0x30
            jr      z,dpa_no_fd1$
            ld      hl,#fd_dev1
            call    dev_append_hl$
dpa_no_fd1$:
            ld      a,d
            and     #0x0c
            jr      z,dpa_no_fd2$
            ld      hl,#fd_dev2
            call    dev_append_hl$
dpa_no_fd2$:
            ld      a,d
            and     #0x03
            jr      z,dpa_no_fd3$
            ld      hl,#fd_dev3
            call    dev_append_hl$
dpa_no_fd3$:

            ld      a,(__sys_nvram_cache + HD_NVRAM_TYPE_BYTE)
            and     #HD_NVRAM_SDA_MASK
            ld      hl,#0x0000
            jr      z,dpa_no_hd$
            ld      hl,#hd_dev0
dpa_no_hd$:
            jp      dev_append_hl$

            ;; ----------------------------------------------------------------
            ;; <hl> *drv, <de> *dev <= __find_dev_drv$(<hl> *name)
            ;; ----------------------------------------------------------------
            ;; finds a device by name in the global device list and returns
            ;; its driver through the dev_t back pointer.
            ;; ----------------------------------------------------------------
__find_dev_drv$:
            ld      de,(_dev_first)
fdd_loop$:
            ld      a,d
            or      e
            jr      z,fdd_fail$
            push    hl
            push    de
            inc     de
            inc     de
fdd_cmp$:
            ld      a,(de)
            cp      (hl)
            jr      nz,fdd_next$
            or      a
            jr      z,fdd_found$
            inc     de
            inc     hl
            jr      fdd_cmp$
fdd_next$:
            pop     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     hl
            jr      fdd_loop$
fdd_found$:
            pop     de
            pop     hl
            ld      hl,#DEV_DRIVER
            add     hl,de
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            xor     a
            ret
fdd_fail$:
            ld      hl,#0x0000
            ld      de,#0x0000
            ld      a,#0xff
            or      a
            ret

__find_dev_drv::
            call    __find_dev_drv$
            ex      de,hl
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> *drv, <de> *dev <= dev_find_by_name(<hl> *name)
            ;; ----------------------------------------------------------------
            ;; assembly-only helper with the raw register contract preserved:
            ;;   - de = matching dev_t* (or 0)
            ;;   - hl = matching dev_drv_t* (or 0)
            ;;
            ;; fat_mount.s needs the device pointer in de so it can hand the
            ;; request straight to fat_mount_common$ without depending on the
            ;; sdcc-facing wrapper's register reshuffle.
            ;; ----------------------------------------------------------------
dev_find_by_name::
            jp      __find_dev_drv$

            .area   _INITIALIZED
_dev_first::
            .dw     0x0000              ; head of the enumerated device chain
_drv_first::
            .dw     ctc_dev_drv         ; head of the built-in driver chain

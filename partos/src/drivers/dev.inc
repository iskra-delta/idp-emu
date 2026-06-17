            ;; dev.inc
            ;;
            ;; include definitions for devices (must match include/dev.h)
            ;;
            ;; 2026-06-11   tstih

            ;; --- limits ------------------------------------------------------
            .equ    DEV_MAX,        16  ; maximum number of device instances

            ;; --- dev_s.flags bits --------------------------------------------
            .equ    DEV_F_OPEN,     0x01 ; instance is open
            .equ    DEV_F_BUSY,     0x02 ; operation in progress
            .equ    DEV_F_ERROR,    0x04 ; last operation failed
            .equ    DEV_F_LOCKED,   0x10 ; locked against concurrent access

            ;; --- standard ioctl commands -------------------------------------
            .equ    IOCTL_GETFLAGS, 0x01 ; output(de): current flags
            .equ    IOCTL_SETFLAGS, 0x02 ; input(de): flags to set
            .equ    IOCTL_CLRFLAGS, 0x03 ; input(de): flags to clear

            ;; next is the FIRST member of both structures so the generic
            ;; list routines (list.s) can handle them uniformly.

            ;; --- struct dev_drv_s offsets (16 bytes) -------------------------
            ;; probe = enumerate units (no hardware init); init = driver-level
            ;; one-time setup (controller/chip), run once at kernel start; open
            ;; = per-device instance init.
            .equ    DRV_NEXT,       0   ; struct dev_drv_s *next (list link)
            .equ    DRV_PROBE,      2   ; probe function pointer
            .equ    DRV_INIT,       4   ; driver init function pointer
            .equ    DRV_OPEN,       6   ; open function pointer
            .equ    DRV_CLOSE,      8   ; close function pointer
            .equ    DRV_READ,       10  ; read function pointer
            .equ    DRV_WRITE,      12  ; write function pointer
            .equ    DRV_IOCTL,      14  ; ioctl function pointer

            ;; --- struct dev_s offsets (20 bytes) -----------------------------
            .equ    DEV_NEXT,       0   ; struct dev_s *next (list link)
            .equ    DEV_NAME,       2   ; name[6], zero-terminated
            .equ    DEV_NAME_SIZE,  6
            .equ    DEV_FLAGS,      8   ; flags
            .equ    DEV_DATA,       9   ; data[9], driver private state
            .equ    DEV_DATA_SIZE,  9
            .equ    DEV_DRIVER,     18  ; dev_drv_t *driver (back pointer)
            .equ    DEV_SIZE,       20  ; sizeof(dev_t)

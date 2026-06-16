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

            ;; --- struct dev_drv_s offsets (14 bytes) -------------------------
            .equ    DRV_NEXT,       0   ; struct dev_drv_s *next (list link)
            .equ    DRV_PROBE,      2   ; probe function pointer
            .equ    DRV_OPEN,       4   ; open function pointer
            .equ    DRV_CLOSE,      6   ; close function pointer
            .equ    DRV_READ,       8   ; read function pointer
            .equ    DRV_WRITE,      10  ; write function pointer
            .equ    DRV_IOCTL,      12  ; ioctl function pointer

            ;; --- struct dev_s offsets (30 bytes) -----------------------------
            .equ    DEV_NEXT,       0   ; struct dev_s *next (list link)
            .equ    DEV_NAME,       2   ; name[8]
            .equ    DEV_RESERVED,   10  ; guaranteed zero terminator
            .equ    DEV_FLAGS,      11  ; flags
            .equ    DEV_DATA,       12  ; data[16], driver private state
            .equ    DEV_DRIVER,     28  ; dev_drv_t *driver (back pointer)
            .equ    DEV_SIZE,       30  ; sizeof(dev_t)

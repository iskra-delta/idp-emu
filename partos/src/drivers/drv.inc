            ;; drv.inc
            ;;
            ;; common bios driver helpers and shared dev.data[] layout
            ;;
            ;; 2026-06-13   tstih

            ;; --- generic driver return codes ---------------------------------
            .equ    DRV_OK,         0x0000
            .equ    DRV_ERR,        0xffff

            ;; --- shared disk-style ioctl commands ----------------------------
            ;; de points to a little-endian 24-bit byte offset buffer.
            .equ    IOCTL_GETPOS,   0x10
            .equ    IOCTL_SETPOS,   0x11

            ;; --- shared dev.data[] offsets -----------------------------------
            .equ    DRV_DATA_UNIT,  0   ; physical unit / target number
            .equ    DRV_DATA_POS0,  1   ; byte offset low
            .equ    DRV_DATA_POS1,  2   ; byte offset mid
            .equ    DRV_DATA_POS2,  3   ; byte offset high

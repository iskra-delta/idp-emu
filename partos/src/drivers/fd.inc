            ;; fd.inc
            ;;
            ;; partner floppy controller constants
            ;;
            ;; 2026-06-13   tstih

            .equ    FD_PORT_MSR,        0xf0
            .equ    FD_PORT_DATA,       0xf1

            .equ    FD_MSR_PHASE_MASK,  0xc0
            .equ    FD_MSR_CMD_READY,   0x80
            .equ    FD_MSR_RES_READY,   0xc0

            .equ    FD_CMD_READ_DATA,   0x06
            .equ    FD_CMD_WRITE_DATA,  0x05
            .equ    FD_CMD_SENSE_DRIVE, 0x04
            .equ    FD_CMD_RECAL,       0x07
            .equ    FD_CMD_SENSE_INT,   0x08
            .equ    FD_CMD_SEEK,        0x0f

            .equ    FD_ST3_T0,          0x10
            .equ    FD_ST3_READY,       0x20
            .equ    FD_ST0_IC_MASK,     0xc0
            .equ    FD_ST0_IC_IC,       0x80

            .equ    FD_SECTRK,          18
            .equ    FD_HEADS,           2
            .equ    FD_SECTOR_SIZE,     256
            .equ    FD_SECTOR_N,        1
            .equ    FD_RW_GPL,          0x1b
            .equ    FD_RW_DTL,          0xff

            .equ    FD_MAX_UNITS,       4
            .equ    FD_POLL_TIMEOUT,    0xffff

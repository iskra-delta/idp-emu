            ;; fd.inc
            ;;
            ;; minimal rom-side floppy profile constants
            ;;
            ;; 2026-06-14   tstih

            .equ    FD_TYPE_COUNT,          4
            .equ    FD_TYPE_F_VALID,        0x01 ; profile slot is populated

            ;; the read geometry the boot reader needs (sectors/track + fdc
            ;; sector-size code) lives in fd_geom_table$ in fd.s, keyed by the
            ;; nvram fd0 type. labels and full geometry stay with the os driver.
            .equ    FD_TYPE_FLAGS,          0
            .equ    FD_TYPE_SIZE,           1

            .equ    FD_NVRAM_TYPE_BYTE,     1
            .equ    FD_NVRAM_FD0_MASK,      0xc0
            .equ    FD_NVRAM_FD1_MASK,      0x30
            .equ    FD_NVRAM_FD2_MASK,      0x0c
            .equ    FD_NVRAM_FD3_MASK,      0x03

            .equ    FD_PORT_MOTOR,          0x98
            .equ    FD_PORT_MSR,            0xf0
            .equ    FD_PORT_DATA,           0xf1

            .equ    FD_MSR_PHASE_MASK,      0xc0
            .equ    FD_MSR_CMD_READY,       0x80
            .equ    FD_MSR_RES_READY,       0xc0

            .equ    FD_CMD_SPECIFY,         0x03
            .equ    FD_CMD_SENSE_DRIVE,     0x04
            .equ    FD_CMD_RECAL,           0x07
            .equ    FD_CMD_SENSE_INT,       0x08
            .equ    FD_CMD_READ,            0x46 ; READ DATA, MFM

            .equ    FD_BOOT_SPT,            18   ; 256-byte sectors per track
            .equ    FD_READ_N,              0x01 ; N=1 -> 256-byte sector
            .equ    FD_READ_GPL,            0x2a
            .equ    FD_READ_DTL,            0xff

            .equ    FD_ST3_READY,           0x20
            .equ    FD_ST0_IC_MASK,         0xc0
            .equ    FD_ST0_IC_IC,           0x80

            .equ    FD_SPECIFY_SRT_HUT,     0xde
            .equ    FD_SPECIFY_HLT_ND,      0x08

            .equ    FD_POLL_TIMEOUT,        0xffff
            .equ    FD_MAX_BOOT_UNITS,      2

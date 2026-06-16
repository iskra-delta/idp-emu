            ;; hd.inc
            ;;
            ;; minimal rom-side hard-disk profile constants
            ;;
            ;; 2026-06-14   tstih

            .equ    HD_TYPE_COUNT,          4
            .equ    HD_TYPE_F_VALID,        0x01 ; profile slot is populated

            ;; each entry is a flags byte followed by the pre-encoded 8-byte
            ;; xebec Initialize Drive Characteristics payload.
            .equ    HD_TYPE_FLAGS,          0
            .equ    HD_TYPE_XEBEC,          1
            .equ    HD_TYPE_SIZE,           9

            .equ    HD_XEBEC_CFG_SIZE,      8

            .equ    HD_PORT_STATUS,         0x10
            .equ    HD_PORT_CTRL,           0x10
            .equ    HD_PORT_DATA,           0x11
            .equ    HD_PORT_RESET,          0x12

            .equ    HD_ST_REQ,              0x80
            .equ    HD_ST_IO,               0x40
            .equ    HD_ST_CD,               0x10
            .equ    HD_ST_BSY,              0x08

            .equ    HD_CTRL_SEL,            0x01
            .equ    HD_CTRL_DATA_EN,        0x02
            .equ    HD_CTRL_DRQ_EN,         0x20

            .equ    HD_STATUS_CMD,          0x90
            .equ    HD_STATUS_DATA_IN,      0xc0
            .equ    HD_STATUS_DATA_OUT,     0x80
            .equ    HD_STATUS_RESP,         0xd0

            .equ    HD_CMD_INITDRV,         0x0c
            .equ    HD_CMD_READ6,           0x08 ; READ(6), 256-byte blocks

            .equ    HD_LUN1,                0x20
            .equ    HD_POLL_TIMEOUT,        0xffff
            .equ    HD_RESET_SPIN,          0x2000

            .equ    HD_NVRAM_TYPE_BYTE,     2
            .equ    HD_NVRAM_SDA_MASK,      0xc0
            .equ    HD_NVRAM_SDB_MASK,      0x30

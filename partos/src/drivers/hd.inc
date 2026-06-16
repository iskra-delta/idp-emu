            ;; hd.inc
            ;;
            ;; partner sasi / xebec hard disk adapter constants
            ;;
            ;; 2026-06-13   tstih

            .equ    HD_PORT_STATUS,     0x10
            .equ    HD_PORT_CTRL,       0x10
            .equ    HD_PORT_DATA,       0x11
            .equ    HD_PORT_RESET,      0x12

            .equ    HD_ST_REQ,          0x80
            .equ    HD_ST_IO,           0x40
            .equ    HD_ST_CD,           0x10
            .equ    HD_ST_BSY,          0x08

            .equ    HD_CTRL_SEL,        0x01
            .equ    HD_CTRL_DATA_EN,    0x02
            .equ    HD_CTRL_DRQ_EN,     0x20

            .equ    HD_STATUS_CMD,      0x90 ; REQ + CD, IO clear
            .equ    HD_STATUS_DATA_IN,  0xc0 ; REQ + IO, CD clear
            .equ    HD_STATUS_DATA_OUT, 0x80 ; REQ, IO clear, CD clear
            .equ    HD_STATUS_RESP,     0xd0 ; REQ + IO + CD

            .equ    HD_CMD_READY,       0x01 ; 1-byte "drive ready" exchange
            .equ    HD_CMD_READ6,       0x08
            .equ    HD_CMD_WRITE6,      0x0a

            .equ    HD_BLOCK_SIZE,      256

            .equ    HD_RESET_DELAY_MS,  10
            .equ    HD_POLL_TIMEOUT,    0xffff

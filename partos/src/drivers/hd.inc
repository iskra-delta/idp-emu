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
            .equ    HD_CMD_INITDRV,     0x0c ; Xebec Initialize Drive Characteristics
            .equ    HD_XEBEC_CFG_SIZE,  8    ; geometry payload bytes

            .equ    HD_BLOCK_SIZE,      256

            .equ    HD_RESET_DELAY_MS,  10
            .equ    HD_POLL_TIMEOUT,    0xffff

            ;; z80 dma end-of-block interrupt vector used by the hd driver.
            ;; dma sits first in the daisy chain, so this vector is delivered
            ;; directly by the dma once the programmed byte count reaches zero.
            .equ    HD_DMA_VEC,         0x90

            ;; nvram byte 2 packs the hard-disk type selectors (0 = absent):
            ;;   sda[7:6] sdb[5:4]
            .equ    HD_NVRAM_TYPE_BYTE, 2
            .equ    HD_NVRAM_SDA_MASK,  0xc0

            ;; z80 dma register port (shared with the floppy controller)
            .equ    HD_PORT_DMA,        0xc0

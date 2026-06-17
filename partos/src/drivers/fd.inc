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

            ;; nvram byte 1 packs the floppy type selectors (0 = absent):
            ;;   fd0[7:6] fd1[5:4] fd2[3:2] fd3[1:0]
            .equ    FD_NVRAM_TYPE_BYTE, 1

            ;; per-unit geometry kept in dev.data[] (the rom delegates floppy
            ;; geometry to the os driver). bytes 0..3 are unit + byte cursor.
            .equ    FD_DATA_SPT,        4   ; sectors per track
            .equ    FD_DATA_N,          5   ; fdc sector-size code (1=256,2=512)
            .equ    FD_DATA_SSH,        6   ; sector size >> 8 (1 or 2)

            ;; --- dma + interrupt driven i/o ----------------------------------
            .equ    FD_PORT_DMA,        0xc0 ; z80 dma register port
            .equ    FD_PORT_INT_VEC,    0xe8 ; fdc im2 vector register
            .equ    FD_VEC,             0xe8 ; fdc vector -> handler at IM2+0xe8
            .equ    FD_CMD_SPECIFY,     0x03
            .equ    FD_SPEC_SRT_HUT,    0xde ; step rate / head unload time
            .equ    FD_SPEC_HLT_ND,     0x08 ; head load time, nd=0 => dma mode
            .equ    FD_DMA_LEN_LO,      0xff ; 256-byte sector, count-1 low
            .equ    FD_DMA_LEN_HI,      0x00 ; count-1 high

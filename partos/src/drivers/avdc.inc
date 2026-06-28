            ;; avdc.inc
            ;;
            ;; Partner GDP-board AVDC text-driver constants.
            ;;
            ;; The primary public names are AVDC_* because the kernel now
            ;; exposes the text path as the "avdc" device. GDP_* aliases remain
            ;; available so older ROM and test sources can keep including the
            ;; legacy file without paying any runtime cost.
            ;;
            ;; 2026-06-17   tstih

            .equ    AVDC_PORT_EF_CMD,       0x20
            .equ    AVDC_PORT_EF_STATUS,    0x2f
            .equ    AVDC_PORT_PIO_COMMON,   0x30
            .equ    AVDC_PORT_PIO_CTRL_A,   0x31
            .equ    AVDC_PORT_PIO_TEXT,     0x32
            .equ    AVDC_PORT_PIO_CTRL_B,   0x33
            .equ    AVDC_PORT_AVDC_DATA,    0x34
            .equ    AVDC_PORT_AVDC_ATTR,    0x35
            .equ    AVDC_PORT_AVDC_COMMON,  0x36
            .equ    AVDC_PORT_AVDC_INIT,    0x38
            .equ    AVDC_PORT_AVDC_STATUS,  0x39
            .equ    AVDC_PORT_AVDC_CUR_LO,  0x3c
            .equ    AVDC_PORT_AVDC_CUR_HI,  0x3d

            .equ    AVDC_F_PIO,             0x01
            .equ    AVDC_F_EF9367,          0x02
            .equ    AVDC_F_AVDC,            0x04

            .equ    AVDC_DATA_PIO_PORT,     0
            .equ    AVDC_DATA_EF_CMD_PORT,  1
            .equ    AVDC_DATA_EF_STAT_PORT, 2
            .equ    AVDC_DATA_AVDC_DATA,    3
            .equ    AVDC_DATA_AVDC_STAT,    4
            .equ    AVDC_DATA_FLAGS,        5
            .equ    AVDC_DATA_TEXT_ATTR,    6
            .equ    AVDC_DATA_CUR_X,        7
            .equ    AVDC_DATA_CUR_Y,        8

            .equ    AVDC_ROWS,              26
            .equ    AVDC_COLS,              132
            .equ    AVDC_TEXT_BASE,         0x0100
            .equ    AVDC_CLEAR_END,         0x1fff

            .equ    AVDC_ATTR_NORMAL,       0x00
            .equ    AVDC_ATTR_UNDERLINE,    0x02
            .equ    AVDC_ATTR_HIGHLIGHT,    0x10
            .equ    AVDC_ATTR_INVERSE,      0x20
            .equ    AVDC_ATTR_MASK,         0x32

            .equ    AVDC_MEM_ACC,           0x10
            .equ    AVDC_STS_RDY,           0x20
            .equ    AVDC_CMD_SET_PTR,       0x1a
            .equ    AVDC_CMD_CURS_OFF,      0x30
            .equ    AVDC_CMD_CURS_ON,       0x31
            .equ    AVDC_CMD_WAC,           0xab
            .equ    AVDC_CMD_RDPTR,         0xa4
            .equ    AVDC_CMD_FILL,          0xbb

            .equ    AVDC_IOCTL_SETATTR,     0x20
            .equ    AVDC_IOCTL_CLEAR,       0x21
            .equ    AVDC_IOCTL_CURSOR,      0x22
            .equ    AVDC_IOCTL_GOTOXY,      0x23

            .equ    AVDC_CURSOR_HIDE,       0
            .equ    AVDC_CURSOR_SHOW,       1

            .equ    AVDC_XY_X,              0
            .equ    AVDC_XY_Y,              1
            .equ    AVDC_XY_SIZE,           2

            ;; legacy GDP_* aliases ------------------------------------------------
            .equ    GDP_PORT_EF_CMD,        AVDC_PORT_EF_CMD
            .equ    GDP_PORT_EF_STATUS,     AVDC_PORT_EF_STATUS
            .equ    GDP_PORT_PIO_COMMON,    AVDC_PORT_PIO_COMMON
            .equ    GDP_PORT_PIO_CTRL_A,    AVDC_PORT_PIO_CTRL_A
            .equ    GDP_PORT_PIO_TEXT,      AVDC_PORT_PIO_TEXT
            .equ    GDP_PORT_PIO_CTRL_B,    AVDC_PORT_PIO_CTRL_B
            .equ    GDP_PORT_AVDC_DATA,     AVDC_PORT_AVDC_DATA
            .equ    GDP_PORT_AVDC_ATTR,     AVDC_PORT_AVDC_ATTR
            .equ    GDP_PORT_AVDC_COMMON,   AVDC_PORT_AVDC_COMMON
            .equ    GDP_PORT_AVDC_INIT,     AVDC_PORT_AVDC_INIT
            .equ    GDP_PORT_AVDC_STATUS,   AVDC_PORT_AVDC_STATUS
            .equ    GDP_PORT_AVDC_CUR_LO,   AVDC_PORT_AVDC_CUR_LO
            .equ    GDP_PORT_AVDC_CUR_HI,   AVDC_PORT_AVDC_CUR_HI

            .equ    GDP_F_PIO,              AVDC_F_PIO
            .equ    GDP_F_EF9367,           AVDC_F_EF9367
            .equ    GDP_F_AVDC,             AVDC_F_AVDC

            .equ    GDP_DATA_PIO_PORT,      AVDC_DATA_PIO_PORT
            .equ    GDP_DATA_EF_CMD_PORT,   AVDC_DATA_EF_CMD_PORT
            .equ    GDP_DATA_EF_STAT_PORT,  AVDC_DATA_EF_STAT_PORT
            .equ    GDP_DATA_AVDC_DATA,     AVDC_DATA_AVDC_DATA
            .equ    GDP_DATA_AVDC_STAT,     AVDC_DATA_AVDC_STAT
            .equ    GDP_DATA_FLAGS,         AVDC_DATA_FLAGS
            .equ    GDP_DATA_TEXT_ATTR,     AVDC_DATA_TEXT_ATTR
            .equ    GDP_DATA_CUR_X,         AVDC_DATA_CUR_X
            .equ    GDP_DATA_CUR_Y,         AVDC_DATA_CUR_Y

            .equ    GDP_ROWS,               AVDC_ROWS
            .equ    GDP_COLS,               AVDC_COLS
            .equ    GDP_TEXT_BASE,          AVDC_TEXT_BASE
            .equ    GDP_CLEAR_END,          AVDC_CLEAR_END

            .equ    GDP_ATTR_NORMAL,        AVDC_ATTR_NORMAL
            .equ    GDP_ATTR_UNDERLINE,     AVDC_ATTR_UNDERLINE
            .equ    GDP_ATTR_HIGHLIGHT,     AVDC_ATTR_HIGHLIGHT
            .equ    GDP_ATTR_INVERSE,       AVDC_ATTR_INVERSE
            .equ    GDP_ATTR_MASK,          AVDC_ATTR_MASK

            .equ    GDP_AVDC_MEM_ACC,       AVDC_MEM_ACC
            .equ    GDP_AVDC_STS_RDY,       AVDC_STS_RDY
            .equ    GDP_AVDC_CMD_SET_PTR,   AVDC_CMD_SET_PTR
            .equ    GDP_AVDC_CMD_CURS_OFF,  AVDC_CMD_CURS_OFF
            .equ    GDP_AVDC_CMD_CURS_ON,   AVDC_CMD_CURS_ON
            .equ    GDP_AVDC_CMD_WAC,       AVDC_CMD_WAC
            .equ    GDP_AVDC_CMD_RDPTR,     AVDC_CMD_RDPTR
            .equ    GDP_AVDC_CMD_FILL,      AVDC_CMD_FILL

            .equ    GDP_IOCTL_SETATTR,      AVDC_IOCTL_SETATTR
            .equ    GDP_IOCTL_CLEAR,        AVDC_IOCTL_CLEAR
            .equ    GDP_IOCTL_CURSOR,       AVDC_IOCTL_CURSOR
            .equ    GDP_IOCTL_GOTOXY,       AVDC_IOCTL_GOTOXY
            .equ    GDP_XY_X,               AVDC_XY_X
            .equ    GDP_XY_Y,               AVDC_XY_Y
            .equ    GDP_XY_SIZE,            AVDC_XY_SIZE

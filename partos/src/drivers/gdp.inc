            ;; gdp.inc
            ;;
            ;; partner gdp board constants
            ;;
            ;; 2026-06-13   tstih

            .equ    GDP_PORT_EF_CMD,        0x20
            .equ    GDP_PORT_EF_STATUS,     0x2f
            .equ    GDP_PORT_PIO_COMMON,    0x30
            .equ    GDP_PORT_PIO_CTRL_A,    0x31
            .equ    GDP_PORT_PIO_TEXT,      0x32
            .equ    GDP_PORT_PIO_CTRL_B,    0x33
            .equ    GDP_PORT_AVDC_DATA,     0x34
            .equ    GDP_PORT_AVDC_ATTR,     0x35
            .equ    GDP_PORT_AVDC_COMMON,   0x36
            .equ    GDP_PORT_AVDC_INIT,     0x38
            .equ    GDP_PORT_AVDC_STATUS,   0x39
            .equ    GDP_PORT_AVDC_CUR_LO,   0x3c
            .equ    GDP_PORT_AVDC_CUR_HI,   0x3d

            .equ    GDP_F_PIO,              0x01
            .equ    GDP_F_EF9367,           0x02
            .equ    GDP_F_AVDC,             0x04

            .equ    GDP_DATA_PIO_PORT,      0
            .equ    GDP_DATA_EF_CMD_PORT,   1
            .equ    GDP_DATA_EF_STAT_PORT,  2
            .equ    GDP_DATA_AVDC_DATA,     3
            .equ    GDP_DATA_AVDC_STAT,     4
            .equ    GDP_DATA_FLAGS,         5
            .equ    GDP_DATA_TEXT_ATTR,     6
            .equ    GDP_DATA_CUR_X,         7
            .equ    GDP_DATA_CUR_Y,         8

            .equ    GDP_ROWS,               26
            .equ    GDP_COLS,               132
            .equ    GDP_TEXT_BASE,          0x0100
            .equ    GDP_CLEAR_END,          0x1fff

            .equ    GDP_ATTR_NORMAL,        0x00
            .equ    GDP_ATTR_HIGHLIGHT,     0x10
            .equ    GDP_ATTR_INVERSE,       0x20
            .equ    GDP_ATTR_MASK,          0x30

            .equ    GDP_AVDC_MEM_ACC,       0x10
            .equ    GDP_AVDC_STS_RDY,       0x20
            .equ    GDP_AVDC_CMD_SET_PTR,   0x1a
            .equ    GDP_AVDC_CMD_CURS_OFF,  0x30
            .equ    GDP_AVDC_CMD_CURS_ON,   0x31
            .equ    GDP_AVDC_CMD_WAC,       0xab
            .equ    GDP_AVDC_CMD_RDPTR,     0xa4
            .equ    GDP_AVDC_CMD_FILL,      0xbb

            .equ    GDP_IOCTL_GETPOS,       0x20
            .equ    GDP_IOCTL_SETPOS,       0x21
            .equ    GDP_IOCTL_GETATTR,      0x22
            .equ    GDP_IOCTL_SETATTR,      0x23
            .equ    GDP_IOCTL_CURSOR_OFF,   0x24
            .equ    GDP_IOCTL_CURSOR_ON,    0x25

            .equ    GDP_POS_X,              0
            .equ    GDP_POS_Y,              1
            .equ    GDP_POS_SIZE,           2

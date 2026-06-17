            ;; sio.inc
            ;;
            ;; partner z80 sio channel constants
            ;;
            ;; 2026-06-13   tstih

            .equ    SIO_CHAN_COUNT,      4
            .equ    SIO_DEFAULT_RX,      32
            .equ    SIO_DEFAULT_TX,      32

            .equ    SIO0A_DATA_PORT,     0xd8
            .equ    SIO0A_CTRL_PORT,     0xd9
            .equ    SIO0B_DATA_PORT,     0xda
            .equ    SIO0B_CTRL_PORT,     0xdb
            .equ    SIO1A_DATA_PORT,     0xe0
            .equ    SIO1A_CTRL_PORT,     0xe1
            .equ    SIO1B_DATA_PORT,     0xe2
            .equ    SIO1B_CTRL_PORT,     0xe3

            .equ    SIO_DATA_PORT_OFF,   0
            .equ    SIO_CTRL_PORT_OFF,   1
            .equ    SIO_CHIP_OFF,        2
            .equ    SIO_CHAN_OFF,        3
            .equ    SIO_STATE_PTR_OFF,   4

            .equ    SIO_RR0_RX_AVAIL,    0x01
            .equ    SIO_RR0_TX_EMPTY,    0x04

            .equ    SIO0_VEC,            0xa0
            .equ    SIO1_VEC,            0xa8

            .equ    SIO_IOCTL_SETBUFS,   0x20
            .equ    SIO_IOCTL_LOCK,      0x21
            .equ    SIO_IOCTL_UNLOCK,    0x22
            .equ    SIO_IOCTL_INITLINE,  0x23

            .equ    SIO_PARITY_NONE,     0
            .equ    SIO_PARITY_EVEN,     1
            .equ    SIO_PARITY_ODD,      2

            .equ    SIO_WRCMD_RESET,     0x18
            .equ    SIO_WRCMD_RTI,       0x38

            .equ    SIO_WR1_DEFAULT,     0x1a ; tx int + rx int all chars
            .equ    SIO_WR3_DEFAULT,     0xc1 ; rx enable, 8 bits
            .equ    SIO_WR4_DEFAULT,     0x44 ; x16, 1 stop, no parity
            .equ    SIO_WR5_DEFAULT,     0xea ; dtr+rts, tx enable, 8 bits

            ;; --- sio private per-channel state ------------------------------
            .equ    SIO_ST_DEV,          0
            .equ    SIO_ST_DATA,         2
            .equ    SIO_ST_CTRL,         3
            .equ    SIO_ST_LOCK,         4
            .equ    SIO_ST_RXBUF,        6
            .equ    SIO_ST_TXBUF,        8
            .equ    SIO_ST_RXSZ,         10
            .equ    SIO_ST_TXSZ,         11
            .equ    SIO_ST_RXHEAD,       12
            .equ    SIO_ST_RXTAIL,       13
            .equ    SIO_ST_RXCOUNT,      14
            .equ    SIO_ST_TXHEAD,       15
            .equ    SIO_ST_TXTAIL,       16
            .equ    SIO_ST_TXCOUNT,      17
            .equ    SIO_ST_RDBUF,        18
            .equ    SIO_ST_RDLEFT,       20
            .equ    SIO_ST_RDEVT,        22
            .equ    SIO_ST_RDOWNER,      24
            .equ    SIO_ST_WRBUF,        26
            .equ    SIO_ST_WRLEFT,       28
            .equ    SIO_ST_WREVT,        30
            .equ    SIO_ST_WROWNER,      32
            .equ    SIO_ST_WRACT,        34
            .equ    SIO_ST_WR3,          35
            .equ    SIO_ST_WR4,          36
            .equ    SIO_ST_WR5,          37
            .equ    SIO_ST_SIZE,         38

            ;; pio.inc
            ;;
            ;; include definitions for the Partner mainboard Z80 PIO driver
            ;;
            ;; 2026-06-17   tstih

            .equ    PIO_CHAN_COUNT,      2
            .equ    PIO_DEFAULT_RX,      32
            .equ    PIO_DEFAULT_TX,      32

            .equ    PIOA_DATA_PORT,      0xd0
            .equ    PIOA_CTRL_PORT,      0xd1
            .equ    PIOB_DATA_PORT,      0xd2
            .equ    PIOB_CTRL_PORT,      0xd3

            .equ    PIO_DATA_PORT_OFF,   0
            .equ    PIO_CTRL_PORT_OFF,   1
            .equ    PIO_STATE_PTR_OFF,   2

            .equ    PIOA_VEC,            0xb0
            .equ    PIOB_VEC,            0xb2

            .equ    PIO_IOCTL_SETBUFS,   0x20
            .equ    PIO_IOCTL_LOCK,      0x21
            .equ    PIO_IOCTL_UNLOCK,    0x22
            .equ    PIO_IOCTL_SETMODE,   0x23

            .equ    PIO_MODE_OUTPUT,     0
            .equ    PIO_MODE_INPUT,      1

            .equ    PIO_CTRL_MODE_OUT,   0x0f
            .equ    PIO_CTRL_MODE_IN,    0x4f
            .equ    PIO_CTRL_INT_EN,     0x87

            ;; private per-port runtime state
            .equ    PIO_ST_DEV,          0
            .equ    PIO_ST_DATA,         2
            .equ    PIO_ST_CTRL,         3
            .equ    PIO_ST_LOCK,         4
            .equ    PIO_ST_RXBUF,        6
            .equ    PIO_ST_TXBUF,        8
            .equ    PIO_ST_RXSZ,         10
            .equ    PIO_ST_TXSZ,         11
            .equ    PIO_ST_RXHEAD,       12
            .equ    PIO_ST_RXTAIL,       13
            .equ    PIO_ST_RXCOUNT,      14
            .equ    PIO_ST_TXHEAD,       15
            .equ    PIO_ST_TXTAIL,       16
            .equ    PIO_ST_TXCOUNT,      17
            .equ    PIO_ST_RDBUF,        18
            .equ    PIO_ST_RDLEFT,       20
            .equ    PIO_ST_RDEVT,        22
            .equ    PIO_ST_RDOWNER,      24
            .equ    PIO_ST_WRBUF,        26
            .equ    PIO_ST_WRLEFT,       28
            .equ    PIO_ST_WREVT,        30
            .equ    PIO_ST_WROWNER,      32
            .equ    PIO_ST_WRACT,        34
            .equ    PIO_ST_MODE,         35
            .equ    PIO_ST_TXWAIT,       36
            .equ    PIO_ST_SIZE,         37

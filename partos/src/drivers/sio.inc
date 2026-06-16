            ;; sio.inc
            ;;
            ;; partner z80 sio channel constants
            ;;
            ;; 2026-06-13   tstih

            .equ    SIO_CHAN_COUNT,      4

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

            .equ    SIO_RR0_RX_AVAIL,    0x01
            .equ    SIO_RR0_TX_EMPTY,    0x04

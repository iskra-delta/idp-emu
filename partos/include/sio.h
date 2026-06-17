/*
 * Documents the Partner Z80 SIO serial controllers. Up to four channels are
 * published as ttyS0..ttyS3 (SIO chip 0 channels A/B at 0xD8..0xDB, chip 1 at
 * 0xE0..0xE3). Presence and attachment come from NVRAM byte 3, two bits per
 * channel: ttyS0[7:6] ttyS1[5:4] ttyS2[3:2] ttyS3[1:0].
 *
 * PartOS treats each channel as one async byte stream. RX is interrupt-backed
 * into a software ring, TX drains from a software queue, and line settings are
 * configured through a compact binary ioctl payload instead of text parsing.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef SIO_H
#define SIO_H

#include <stdint.h>

/* Per-channel data/control I/O ports. */
#define SIO0A_DATA_PORT   0xD8
#define SIO0A_CTRL_PORT   0xD9
#define SIO0B_DATA_PORT   0xDA
#define SIO0B_CTRL_PORT   0xDB
#define SIO1A_DATA_PORT   0xE0
#define SIO1A_CTRL_PORT   0xE1
#define SIO1B_DATA_PORT   0xE2
#define SIO1B_CTRL_PORT   0xE3

#define SIO_CHAN_COUNT    4

/* IM2 vectors emitted by the two SIO chips. */
#define SIO0_VEC          0xA0
#define SIO1_VEC          0xA8

/* NVRAM byte 3 packs the serial attach selectors (2 bits each, 0 = absent). */
#define SIO_NVRAM_ATTACH_BYTE  3

/* Serial attach selector values. */
#define SIO_ATTACH_KEYBOARD  0
#define SIO_ATTACH_TERMINAL  1
#define SIO_ATTACH_MOUSE     2
#define SIO_ATTACH_FREE      3

/* Stream ioctls. */
#define SIO_IOCTL_SETBUFS   0x20
#define SIO_IOCTL_LOCK      0x21
#define SIO_IOCTL_UNLOCK    0x22
#define SIO_IOCTL_INITLINE  0x23

/* Parity selector for sio_line_cfg_t. */
#define SIO_PARITY_NONE     0
#define SIO_PARITY_EVEN     1
#define SIO_PARITY_ODD      2

typedef struct sio_bufcfg_s {
    uint8_t rx_ring_size;
    uint8_t tx_queue_size;
} sio_bufcfg_t;

typedef struct sio_linecfg_s {
    uint16_t baud;
    uint8_t  data_bits;
    uint8_t  parity;
    uint8_t  stop_bits;
} sio_linecfg_t;

#endif /* SIO_H */

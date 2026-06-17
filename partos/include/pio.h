/*
 * Public interface for the Partner mainboard Z80 PIO at 0xD0..0xD3.
 *
 * The kernel publishes the two ports as stream-style devices and drives them
 * through PIO interrupts. Output mode uses the peripheral acknowledge cycle;
 * input mode uses the port strobe to latch incoming bytes into a software RX
 * ring.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef PIO_H
#define PIO_H

#include <stdint.h>

/* Mainboard PIO ports. */
#define PIOA_DATA_PORT    0xD0
#define PIOA_CTRL_PORT    0xD1
#define PIOB_DATA_PORT    0xD2
#define PIOB_CTRL_PORT    0xD3

/* IM2 vectors emitted by the two PIO ports. */
#define PIOA_VEC          0xB0
#define PIOB_VEC          0xB2

/* Stream ioctls. */
#define PIO_IOCTL_SETBUFS 0x20
#define PIO_IOCTL_LOCK    0x21
#define PIO_IOCTL_UNLOCK  0x22
#define PIO_IOCTL_SETMODE 0x23

/* Stream-facing subset of the PIO operating modes. */
#define PIO_MODE_OUTPUT   0
#define PIO_MODE_INPUT    1

typedef struct pio_bufcfg_s {
    uint8_t rx_ring_size;
    uint8_t tx_queue_size;
} pio_bufcfg_t;

typedef struct pio_modecfg_s {
    uint8_t mode;
} pio_modecfg_t;

#endif /* PIO_H */

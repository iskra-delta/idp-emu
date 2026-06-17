/*
 * Documents the Partner SASI/Xebec hard-disk adapter (status/control 0x10,
 * data 0x11, reset 0x12). Drives are published as sda/sdb; presence and type
 * come from NVRAM byte 2: sda[7:6] sdb[5:4] (0 = absent).
 *
 * read()/write() are asynchronous via DMA (port 0xC0): one SASI READ(6)/
 * WRITE(6) moves the whole request, the data phase streamed by the DMA, and
 * DMA end-of-block raises IM2 vector 0x90 so the kernel ISR can finish the
 * Xebec response phase and signal the caller's event.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef HD_H
#define HD_H

#include <stdint.h>

/* SASI adapter I/O ports, and the shared DMA port. */
#define HD_PORT_STATUS  0x10
#define HD_PORT_CTRL    0x10
#define HD_PORT_DATA    0x11
#define HD_PORT_RESET   0x12
#define HD_PORT_DMA     0xC0
#define HD_DMA_VEC      0x90

#define HD_BLOCK_SIZE   256

/* NVRAM byte 2 packs the hard-disk type selectors (0 = absent). */
#define HD_NVRAM_TYPE_BYTE  2

/* Hard-disk type selector values. */
#define HD_TYPE_FREE    0
#define HD_TYPE_ST506   1
#define HD_TYPE_ST412   2
#define HD_TYPE_ST225   3

#endif /* HD_H */

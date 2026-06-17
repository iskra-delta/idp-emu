/*
 * Documents the Partner Intel i8272 floppy controller. Up to four units are
 * published as fd0..fd3; presence comes from NVRAM byte 1, two bits per unit:
 * fd0[7:6] fd1[5:4] fd2[3:2] fd3[1:0] (0 = absent).
 *
 * read()/write() are asynchronous: the controller runs in DMA mode (port 0xC0)
 * and raises its IM2 completion interrupt (vector 0xE8 -> handler 0xFD8E) as
 * each sector transfers; the driver chains sectors and signals the caller's
 * event when the whole request is done.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef FD_H
#define FD_H

#include <stdint.h>

/* i8272 main status / data registers, and the shared DMA port. */
#define FD_PORT_MSR   0xF0
#define FD_PORT_DATA  0xF1
#define FD_PORT_DMA   0xC0
#define FD_PORT_VEC   0xE8      /* FDC IM2 vector register */
#define FD_VEC        0xE8      /* FDC vector -> IM2 slot 0xFD00+0xE8 */

/* Physical geometry: 80 cylinders x 2 heads x 18 sectors x 256 bytes. */
#define FD_SECTOR_SIZE  256
#define FD_SECTRK       18
#define FD_HEADS        2
#define FD_MAX_UNITS    4

/* NVRAM byte 1 packs the floppy type selectors (0 = absent). */
#define FD_NVRAM_TYPE_BYTE  1

/* Floppy type selector values. */
#define FD_TYPE_FREE      0
#define FD_TYPE_PARTNER   1
#define FD_TYPE_DOS_720K  2
#define FD_TYPE_DOS_360K  3

#endif /* FD_H */

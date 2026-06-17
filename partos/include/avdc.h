/*
 * Public interface for the PartOS AVDC text driver on the Partner GDP board.
 *
 * The kernel publishes this device as "avdc". It exposes a text console over
 * the SCN2674 controller with row-table scrolling, readable screen memory, and
 * a tiny ioctl set aimed at plain text applications.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef AVDC_H
#define AVDC_H

#include <stdint.h>

/* Board-local I/O ports. */
#define AVDC_PORT_EF_CMD       0x20
#define AVDC_PORT_EF_STATUS    0x2F
#define AVDC_PORT_PIO_COMMON   0x30
#define AVDC_PORT_AVDC_DATA    0x34
#define AVDC_PORT_AVDC_STATUS  0x39

/* Capability flags stored in dev->data[]. */
#define AVDC_F_PIO         0x01
#define AVDC_F_EF9367      0x02
#define AVDC_F_AVDC        0x04

/* Text geometry. */
#define AVDC_ROWS          26
#define AVDC_COLS          132

/* Text attributes. */
#define AVDC_ATTR_NORMAL       0x00
#define AVDC_ATTR_HIGHLIGHT    0x10
#define AVDC_ATTR_INVERSE      0x20

/* Cursor control payload for AVDC_IOCTL_CURSOR. */
#define AVDC_CURSOR_HIDE   0
#define AVDC_CURSOR_SHOW   1

/* ioctl commands. */
#define AVDC_IOCTL_SETATTR  0x20
#define AVDC_IOCTL_CLEAR    0x21
#define AVDC_IOCTL_CURSOR   0x22
#define AVDC_IOCTL_GOTOXY   0x23

typedef struct avdc_xy_s {
    uint8_t x;
    uint8_t y;
} avdc_xy_t;

/*
 * Legacy GDP_* aliases kept so existing user code can migrate incrementally.
 * New code should prefer the AVDC_* names above.
 */
#define GDP_PORT_EF_CMD        AVDC_PORT_EF_CMD
#define GDP_PORT_EF_STATUS     AVDC_PORT_EF_STATUS
#define GDP_PORT_PIO_COMMON    AVDC_PORT_PIO_COMMON
#define GDP_PORT_AVDC_DATA     AVDC_PORT_AVDC_DATA
#define GDP_PORT_AVDC_STATUS   AVDC_PORT_AVDC_STATUS

#define GDP_F_PIO              AVDC_F_PIO
#define GDP_F_EF9367           AVDC_F_EF9367
#define GDP_F_AVDC             AVDC_F_AVDC

#define GDP_ROWS               AVDC_ROWS
#define GDP_COLS               AVDC_COLS

#define GDP_ATTR_NORMAL        AVDC_ATTR_NORMAL
#define GDP_ATTR_HIGHLIGHT     AVDC_ATTR_HIGHLIGHT
#define GDP_ATTR_INVERSE       AVDC_ATTR_INVERSE

#define GDP_IOCTL_SETATTR      AVDC_IOCTL_SETATTR
#define GDP_IOCTL_CLEAR        AVDC_IOCTL_CLEAR
#define GDP_IOCTL_CURSOR       AVDC_IOCTL_CURSOR
#define GDP_IOCTL_GOTOXY       AVDC_IOCTL_GOTOXY

typedef avdc_xy_t gdp_xy_t;

#endif /* AVDC_H */

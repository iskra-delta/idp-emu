/*
 * Declares the shared kernel vector-table interface used to query and
 * replace reset, interrupt, and NMI handlers at runtime.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef VECTOR_H
#define VECTOR_H

#include <stdint.h>

/*
 * Public vector ids accepted by vector_set() ARE the low-page slot addresses:
 * each rst/nmi slot holds a `jp <handler>` that vector_set patches in place, so
 * there is no separate table. (rst 0x00 is the non-vectorable cold entry; the
 * 50 Hz tick hook is reached by a software call from the VBL ISR, not a rst, so
 * it is installed separately and is not a vector here.)
 */
#define VECTOR_RST08    0x08   /* rst 0x08 (default: kernel-API trap) */
#define VECTOR_RST10    0x10   /* rst 0x10 */
#define VECTOR_RST18    0x18   /* rst 0x18 */
#define VECTOR_RST20    0x20   /* rst 0x20 */
#define VECTOR_RST28    0x28   /* rst 0x28 */
#define VECTOR_RST30    0x30   /* rst 0x30 */
#define VECTOR_RST38    0x38   /* im 1 interrupt entry */
#define VECTOR_NMI      0x66   /* nmi entry */

/*
 * Patch one low-page vector slot's jp operand. No return value -- use
 * vector_get() to read a slot's current handler.
 */
void vector_set(uint8_t vector, void *handler);

#endif /* VECTOR_H */

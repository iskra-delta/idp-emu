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
 * Public vector-table indices accepted by vector_get() and vector_set().
 */
#define VECTOR_ENTRY    0   /* rst 0x00 shared entry dispatch */
#define VECTOR_RST08    1   /* rst 0x08 */
#define VECTOR_RST10    2   /* rst 0x10 */
#define VECTOR_RST18    3   /* rst 0x18 */
#define VECTOR_RST20    4   /* rst 0x20 */
#define VECTOR_RST28    5   /* rst 0x28 */
#define VECTOR_RST30    6   /* rst 0x30 */
#define VECTOR_RST38    7   /* im 1 interrupt entry */
#define VECTOR_NMI      8   /* nmi entry */
#define VECTOR_COUNT    9

/*
 * Return the current handler stored in one vector-table slot.
 * Returns NULL if the vector index is invalid.
 */
void *vector_get(uint8_t vector);

/*
 * Replace one vector-table slot and return the previous handler.
 * Returns NULL if the vector index is invalid.
 */
void *vector_set(uint8_t vector, void *handler);

#endif /* VECTOR_H */

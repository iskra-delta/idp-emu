/*
 * Declares the base system-object header used by kernel-owned resources
 * that participate in intrusive tracking lists and heap ownership.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef SYSOBJ_H
#define SYSOBJ_H

#include <stdint.h>

#include "list.h"

/*
 * System-owned allocator heap supplied by the kernel.
 */
extern void *_sys_heap;

/*
 * Base header shared by all tracked kernel system objects.
 */
typedef struct sysobj_s {
    union {
        list_item_t hdr;
        void *next;
    };
    void *owner;
} sysobj_t;

/*
 * Internal system-object helpers layered on top of the kernel heap.
 */
void *__so_create(void **first, uint16_t size, void *owner);

/*
 * Unlink one system object from the supplied intrusive list and free it.
 */
void *__so_destroy(void **first, void *so);

#endif /* SYSOBJ_H */

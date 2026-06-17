/*
 * Declares the kernel heap allocator interface and on-heap block layout
 * used for system-owned memory allocation.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#include "sysobj.h"

#ifndef NONE
#define NONE 0
#endif

/*
 * Size of the heap block header before the payload begins.
 */
#define BLK_SIZE        (sizeof(struct block_s) - sizeof(uint8_t[1]))

/*
 * Smallest payload fragment worth keeping when splitting a free block.
 */
#define MIN_CHUNK_SIZE  4

/*
 * Heap block state flags.
 */
#define NEW             0x00
#define ALLOCATED       0x01

/*
 * One intrusive heap block tracked by the kernel allocator.
 * Free and allocated chunks share this same on-heap layout.
 */
typedef struct block_s {
    sysobj_t hdr;
    uint8_t  stat;
    uint16_t size;
    uint8_t  data[1];
} block_t;

/*
 * Allocator heaps supplied by the kernel.
 */
extern void *_sys_heap;
extern void *_usr_heap;

/*
 * Initialize one allocator heap region.
 */
void mem_init(void *heap, uint16_t size);

/*
 * Allocate one payload block from the given heap.
 */
void *mem_allocate(void *heap, uint16_t size, void *owner);

/*
 * Free one previously allocated payload block.
 */
void *mem_free(void *heap, void *p);

/*
 * Free all blocks owned by the supplied owner id.
 */
uint8_t mem_free_owner(void *heap, void *owner);

#endif /* MEM_H */

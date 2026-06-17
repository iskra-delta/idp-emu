/*
 * Declares the PartOS process object and the minimal process-loader helpers.
 *
 * A process is the resource-owning container above one or more threads. The
 * current kernel keeps ownership in the sysobj header, lets thread_t point
 * back to its parent process, and reaps the process once the last thread has
 * gone away.
 *
 * PartOS currently supports two launch paths:
 *
 * - `process_start()` for a direct entry point already resolved in memory
 * - `process_load_image()` for a relocatable XL image already loaded into RAM
 *
 * `process_load_image()` relocates the image in place, computes the true entry
 * address, starts the process, and reassigns the heap block owner to the new
 * process so the image is freed automatically at process teardown. Therefore
 * the supplied `img` buffer must come from the user heap.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#include "sysobj.h"
#include "thread.h"

/*
 * Maximum process-name length including the terminating NUL.
 */
#define MAX_PNAME_LEN               8

/*
 * Internal/resident process flag.
 */
#define PROCESS_INTERNAL            0x01

/*
 * XL relocatable image header layout.
 */
#define XL_HDR_SIZE                 12
#define XL_RELOC_SIZE               4
#define XL_OFF_MAGIC0               0
#define XL_OFF_MAGIC1               1
#define XL_OFF_VERSION              2
#define XL_OFF_FLAGS                3
#define XL_OFF_ENTRY                4
#define XL_OFF_CODE_SIZE            6
#define XL_OFF_RELOC_CNT            8

/*
 * Process-loader status codes.
 */
#define PROCESS_LOAD_OK             0
#define PROCESS_LOAD_ERR_NOT_FOUND  1
#define PROCESS_LOAD_ERR_ALLOC      2
#define PROCESS_LOAD_ERR_READ       3
#define PROCESS_LOAD_ERR_XL_INVALID 4
#define PROCESS_LOAD_ERR_XL_START   5

/*
 * Process object. Layout must match src/os/process.inc:
 *   +0 next / +2 owner (sysobj), +4 pflags, +5 pname[8], +13 main_thread.
 */
typedef struct process_s {
    sysobj_t hdr;                       /* process is a system object */
    uint8_t  pflags;                    /* PROCESS_* flags */
    char     pname[MAX_PNAME_LEN];      /* name, max 7 chars + NUL */
    thread_t *main_thread;              /* bootstrap/main thread, or NULL */
} process_t;

/*
 * Head of the global process list and the last loader error.
 */
extern process_t *process_first;
extern uint8_t process_last_error;

/*
 * Start one process from an already resolved entry point.
 */
process_t *process_start(
    const char *pname,
    void (*entry_point)(void),
    uint16_t stack_size);

/*
 * Relocate one XL image in place. Returns 0 on success, non-zero on failure.
 */
uint8_t process_relocate(uint8_t *img);

/*
 * Start one process from an already-loaded XL image in RAM.
 *
 * `img` must point at an XL header followed by its relocation table and
 * payload. `img_size` must cover the whole image. The image buffer must come
 * from the user heap so ownership can be transferred to the new process.
 */
process_t *process_load_image(
    const char *pname,
    uint8_t *img,
    uint16_t img_size,
    uint16_t stack_size);

/*
 * Reap one process if it no longer has live threads.
 */
void process_reap(process_t *p);

/*
 * Exit the current process by terminating the current thread.
 */
void process_exit(void);

#endif /* PROCESS_H */

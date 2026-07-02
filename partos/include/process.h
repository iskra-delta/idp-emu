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

#ifndef PARTOS_XL_HEADER_DEFINED
#define PARTOS_XL_HEADER_DEFINED
typedef struct xl_header_s {
    char     magic[2];
    uint8_t  version;
    uint8_t  flags;
    uint16_t entry;
    uint16_t code_size;
    uint16_t reloc_count;
    uint16_t reserved;
} xl_header_t;
#endif

/*
 * Process-loader status codes.
 */
#define PROCESS_LOAD_OK             0
#define PROCESS_LOAD_ERR_NOT_FOUND  1
#define PROCESS_LOAD_ERR_ALLOC      2
#define PROCESS_LOAD_ERR_READ       3
#define PROCESS_LOAD_ERR_XL_INVALID 4
#define PROCESS_LOAD_ERR_XL_START   5
#define PROCESS_LOAD_ERR_COM_INVALID 6

/*
 * COM wrapper header around one embedded XL image.
 */
#define COM_HDR_SIZE                16
#define COM_OFF_MAGIC0              0
#define COM_OFF_MAGIC1              1
#define COM_OFF_VERSION             2
#define COM_OFF_FLAGS               3
#define COM_OFF_STACK_SIZE          4
#define COM_OFF_ENTRY_HINT          6
#define COM_OFF_XL_OFFSET           8
#define COM_OFF_XL_SIZE             10
#define COM_OFF_RESERVED0           12
#define COM_OFF_RESERVED1           14

#ifndef PARTOS_COM_HEADER_DEFINED
#define PARTOS_COM_HEADER_DEFINED
typedef struct com_header_s {
    char     magic[2];
    uint8_t  version;
    uint8_t  flags;
    uint16_t stack_size;
    uint16_t entry_hint;
    uint16_t xl_offset;
    uint16_t xl_size;
    uint16_t reserved0;
    uint16_t reserved1;
} com_header_t;
#endif

/*
 * Process object. Layout must match src/kernel/process.inc:
 *   +0 next / +2 owner (sysobj), +4 pflags, +5 pname[8],
 *   +13 main_thread, +15 cmdline, +17 environment.
 */
typedef struct process_s {
    sysobj_t hdr;                       /* process is a system object */
    uint8_t  pflags;                    /* PROCESS_* flags */
    char     pname[MAX_PNAME_LEN];      /* name, max 7 chars + NUL */
    thread_t *main_thread;              /* bootstrap/main thread, or NULL */
    const char *cmdline;                /* original launch command line, or "" */
    const char *environment;            /* NUL-separated NAME=VALUE block */
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
 * Internal XL-image relocator used by the process loader.
 *
 * Returns 0 on success, non-zero on failure.
 */
uint8_t __process_relocate(uint8_t *img);

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
 * Start one process from a COM wrapper that embeds a relocatable XL image.
 *
 * The COM header carries the expected stack size and the span of the embedded
 * XL payload. As with process_load_image(), the backing buffer must come from
 * the user heap so ownership can be transferred to the new process.
 */
process_t *process_load_com(
    const char *pname,
    uint8_t *img,
    uint16_t img_size);

/*
 * Cooperatively wait until a process has fully exited and been reaped.
 *
 * The current shell uses this to keep launched commands in the foreground.
 * Passing NULL is a no-op that succeeds.
 */
int16_t process_wait(process_t *p);

/*
 * Internal process teardown helper.
 *
 * Reap one process once it no longer has live threads.
 */
void __process_reap(process_t *p);

/*
 * Exit the current process by terminating the current thread.
 */
void process_exit(void);

#endif /* PROCESS_H */

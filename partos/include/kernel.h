/*
 * kernel.h
 *
 * PartOS kernel ABI (the kernel system-call surface).
 *
 * The kernel exports its primitives as ONE function-pointer table whose layout
 * is the kernel_t struct below. A consumer (the OS, or a bare program that
 * brings its own runtime) obtains a pointer to that table and calls through it:
 *
 *     const kernel_t *k = kernel_api();     // rst 0x08 -> &kernel_table
 *     sysvars_t *sv = k->get_sys_vars();
 *     void *p = k->allocate_memory(sv->sys_heap, 64, NONE);
 *
 * The field order here MUST stay in sync with _kernel_table in
 * src/kernel/kernel.s.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

/*
 * kernel.h is a fully self-contained ABI header: the OS includes ONLY this
 * file. It carries the complete definitions of every kernel object type and
 * constant a consumer needs -- nothing else has to be included.
 *
 * Each type block below is guarded by its originating subsystem header's
 * include guard (LIST_H, SYSOBJ_H, EVT_H, ...) and defines that guard, so this
 * header and any of those subsystem headers may be included together, in any
 * order, without a redefinition conflict.
 */

/* --- intrusive list node (list.h) --- */
#ifndef LIST_H
#define LIST_H
typedef struct list_item_s {
    struct list_item_s *next;
    uint8_t             data[0];
} list_item_t;
typedef list_item_t list_t;                 /* legacy alias */
#endif

/* --- system-object base header (sysobj.h) --- */
#ifndef SYSOBJ_H
#define SYSOBJ_H
typedef struct sysobj_s {
    union {
        list_item_t hdr;                    /* tracked in an intrusive list */
        void       *next;
    };
    void *owner;                            /* owning resource, or NONE */
} sysobj_t;
#endif

/* --- sync event (evt.h) --- */
#ifndef EVT_H
#define EVT_H
typedef enum event_state_e {
    nonsignaled = 0,
    signaled    = 1
} event_state_t;
typedef struct event_s {
    sysobj_t hdr;                           /* event is a system object */
    uint8_t  state;                         /* 0 = non-signaled, 1 = signaled */
} event_t;
#endif

/*
 * Named services and soft timers now live in the shared kernel reserve.
 * Higher-level OS code still wires the RST 0x10 bridge and drives the timer
 * chain from its interrupt path, but the registries themselves are kernel
 * facilities and are exported through kernel_t below.
 */

/* --- named service (service.h) --- */
#ifndef SERVICE_H
#define SERVICE_H
#define MAX_SVC_NAME_LEN 16
typedef struct service_s {
    sysobj_t hdr;                           /* service is a system object */
    char     name[MAX_SVC_NAME_LEN];        /* service name */
    void    *fntable;                       /* syscall function table */
} service_t;
#endif

/* --- soft timer (timer.h) --- */
#ifndef TIMER_H
#define TIMER_H
#define EVERYTIME 0
typedef struct timer_s {
    sysobj_t hdr;                           /* timer is a system object */
    void   (*hook)(void);                   /* hook routine */
    uint16_t ticks;                         /* reload value (fires every ticks+1) */
    uint16_t _tick_count;                   /* internal countdown */
} timer_t;
#endif

/* --- thread (thread.h) --- */
#ifndef THREAD_H
#define THREAD_H
#define THREAD_STATE_SUSPENDED   0
#define THREAD_STATE_RUNNING     1
#define THREAD_STATE_WAITING     2
#define THREAD_STATE_JOINED      3
#define THREAD_STATE_TERMINATED  4
#define CONTEXT_SIZE             22         /* af,bc,de,hl,ix,iy,af',bc',de',hl' + ret */
typedef struct thread_s {
    sysobj_t hdr;                           /* thread is a system object */
    uint16_t sp;                            /* saved stack pointer (context on stack) */
    uint8_t  startup[10];                   /* bootstrap: call entry / ld hl,t / jp exit */
    event_t **wait;                         /* events the thread is blocked on, or NULL */
    uint8_t  num_events;                    /* number of events in wait[] */
    uint8_t  state;                         /* THREAD_STATE_* */
    struct thread_s **joined;               /* joined threads (unused for now) */
    void    *thread_data;                   /* OS-opaque pointer the kernel only
                                               stores (typically the process) */
    uint8_t  bank;                          /* RAM bank the thread runs in */
    event_t *wait_inline;                   /* shared one-event wait mirror */
} thread_t;
#endif

/* --- process (process.h) --- */
#ifndef PROCESS_H
#define PROCESS_H
#define MAX_PNAME_LEN               8
#define PROCESS_INTERNAL            0x01
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
#define PROCESS_LOAD_OK             0
#define PROCESS_LOAD_ERR_NOT_FOUND  1
#define PROCESS_LOAD_ERR_ALLOC      2
#define PROCESS_LOAD_ERR_READ       3
#define PROCESS_LOAD_ERR_XL_INVALID 4
#define PROCESS_LOAD_ERR_XL_START   5
#define PROCESS_LOAD_ERR_COM_INVALID 6
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
typedef struct process_s {
    sysobj_t hdr;                           /* process is a system object */
    uint8_t  pflags;                        /* PROCESS_* flags */
    char     pname[MAX_PNAME_LEN];          /* name, max 7 chars + NUL */
    thread_t *main_thread;                  /* bootstrap/main thread, or NULL */
    const char *cmdline;                    /* original launch command line, or "" */
    const char *environment;                /* NUL-separated NAME=VALUE block */
} process_t;
#endif

/*
 * No-owner / null sentinel, accepted wherever an owner argument is taken
 * (allocate_memory, create_event, create_object, ...). Also the value returned
 * for "no object" from the calls that yield a pointer.
 */
#ifndef NONE
#define NONE 0
#endif

/*
 * Vector ids for set_vector() / get_vector() ARE the low-page slot addresses:
 * each rst/nmi slot holds a `jp <handler>` that set_vector patches in place, so
 * there is no separate table. rst 0x00 is the cold entry (not vectorable) and
 * rst 0x08 defaults to the kernel-API trap (returns &kernel_table). The 50 Hz
 * TICK hook is NOT a rst -- it is reached by a software call from the VBL ISR,
 * so it is installed separately (kernel-internal __sys_vec_tick cell), not via
 * set_vector. (Mirrors vector.h; guarded so including both is harmless.)
 */
#ifndef VECTOR_H
#define VECTOR_H
#define VECTOR_RST08    0x08   /* rst 0x08 (default: kernel-API trap) */
#define VECTOR_RST10    0x10   /* rst 0x10 */
#define VECTOR_RST18    0x18   /* rst 0x18 */
#define VECTOR_RST20    0x20   /* rst 0x20 */
#define VECTOR_RST28    0x28   /* rst 0x28 */
#define VECTOR_RST30    0x30   /* rst 0x30 */
#define VECTOR_RST38    0x38   /* im 1 interrupt entry */
#define VECTOR_NMI      0x66   /* nmi entry */
#endif

/*
 * Kernel-published system-variables block. The kernel fills the IM2 table
 * address and the addresses of its live list heads; the OS may write back the
 * heap bases it declares. The "first"/"current" entries are ADDRESSES of the
 * kernel head pointers, read live, e.g.  thread_t *t = *k->get_sys_vars()->thread_running;
 */
typedef struct sysvars_s {
    void       *sys_heap;          /* shared/system heap base (always-mapped)   */
    void       *bank1_heap;        /* bank 1 process-arena heap base            */
    void       *bank2_heap;        /* bank 2 process-arena heap base            */
    void       *im2_table;         /* IM2 interrupt vector table base           */
    void       *list_heads;        /* base of the kernel list-head block; index */
                                   /* with SYSVAR_LH_* below (each a thread_t   */
                                   /* event_t**), e.g. *(thread_t**)((char*)     */
                                   /* sv->list_heads + SYSVAR_LH_THREAD_RUNNING) */
} sysvars_t;

/* byte offsets into sysvars.list_heads -- the kernel's live thread/event head
 * block. service, timer and process registries also live in the kernel now,
 * but they are kept as separate globals rather than packed into this one block. */
#define SYSVAR_LH_THREAD_CURRENT     0
#define SYSVAR_LH_THREAD_SUSPENDED   2
#define SYSVAR_LH_THREAD_RUNNING     4
#define SYSVAR_LH_THREAD_WAITING     6
#define SYSVAR_LH_THREAD_TERMINATED  8
#define SYSVAR_LH_EVENT_LIST        10

/*
 * The kernel ABI table. The kernel exports one instance; its field order MUST
 * match _kernel_table in src/kernel/kernel.s. Every owner/process/cleanup
 * argument accepts NONE; calls that yield a pointer return NONE on failure.
 */
typedef struct kernel_s {
    /* system variables */
    sysvars_t *(*get_sys_vars)(void);

    /* memory (heap from get_sys_vars()->{sys,bank1,bank2}_heap) */
    void *(*allocate_memory)(void *heap, uint16_t size, void *owner);
    void *(*deallocate_memory)(void *heap, void *block);

    /* events (block/wake primitive) */
    event_t *(*create_event)(void *owner);
    event_t *(*destroy_event)(event_t *e);
    event_t *(*set_event)(event_t *e, uint8_t signalled);   /* signalled: 1/0 */
    uint8_t  (*is_signalled)(event_t *e);

    /* intrusive lists */
    list_item_t *(*insert_list)(list_item_t **first, list_item_t *el);
    list_item_t *(*remove_list)(list_item_t **first, list_item_t *el);
    list_item_t *(*find_list)(list_item_t *first, list_item_t **prev,
                              uint8_t (*match)(list_item_t *p, uint16_t arg),
                              uint16_t arg);
    void         (*iterate_list)(list_item_t *first,
                                 void (*fn)(list_item_t *p, uint16_t arg),
                                 uint16_t arg);
    uint8_t      (*match_list_eq)(list_item_t *p, uint16_t arg);

    /* threads */
    thread_t *(*create_thread)(void (*entry)(void), uint16_t stack_size,
                               uint8_t bank, void *thread_data);
    thread_t *(*resume_thread)(thread_t *t);
    thread_t *(*suspend_thread)(thread_t *t);
    void      (*exit_thread)(void);
    void      (*wait_events)(event_t **events, uint8_t count);

    /* tracked objects (shared-heap, owner-tracked convenience wrappers) */
    void *(*create_object)(void **first, uint16_t size, void *owner);
    void *(*destroy_object)(void **first, void *object);
    void  (*set_cleanup)(void *obj, void (*cleanup)(void *obj));  /* NULL = mem only */
    void  (*register_owner_cleanup)(void (*fn)(void *owner));

    /* interrupt vectors */
    void  (*set_vector)(uint8_t vector, void *handler);  /* read back via get_vector */
    void *(*get_vector)(uint8_t vector);

    /* spin locks (lock = pointer to a one-byte cell) */
    uint8_t (*acquire_lock)(void *lock);    /* 1 = acquired, 0 = busy */
    void    (*release_lock)(void *lock);
    uint8_t (*test_lock)(void *lock);       /* 1 = held, 0 = free */

    /* appended to preserve every older offset above */
    /* named services */
    service_t *(*register_service)(const char *name, void *fntable);
    service_t *(*unregister_service)(service_t *s);
    void      *(*query_service)(const char *name);

    /* soft timers */
    timer_t *(*install_timer)(void (*hook)(void), uint16_t ticks, void *owner);
    timer_t *(*uninstall_timer)(timer_t *t);
    void     (*chain_timers)(void);

    /* processes */
    process_t *(*start_process)(const char *pname,
                                void (*entry_point)(void),
                                uint16_t stack_size);
    process_t *(*load_process_image)(const char *pname,
                                     uint8_t *img,
                                     uint16_t img_size,
                                     uint16_t stack_size);
    process_t *(*load_process_com)(const char *pname,
                                   uint8_t *img,
                                   uint16_t img_size);
    int16_t    (*wait_process)(process_t *p);
    void       (*exit_process)(void);

    /* small utility helpers */
    uint8_t (*bcd_to_bin)(uint8_t bcd);
    uint8_t (*bin_to_bcd)(uint8_t value);
    void    (*delay_1ms)(void);

    /* appended (keep at the very end -- offsets above must never move) so the
     * OS can reach EVERY kernel primitive it needs through this one table
     * (rst 0x08), rather than by any link-time absolute address. */
    void  (*disable_interrupts)(void);                        /* ir_disable */
    void  (*enable_interrupts)(void);                         /* ir_enable  */
    void *(*set_irq_vector)(uint8_t vector, void *handler);   /* ir_set     */
    list_item_t *(*append_list)(list_item_t **first, list_item_t *chain);
    void  (*initialize_memory)(void *heap, uint16_t size);    /* mem_init   */
} kernel_t;

/*
 * Obtaining the table: issue `rst 0x08`, which returns hl = &kernel_table. A
 * trusted OS (always-mapped alongside the kernel) may also reference the
 * kernel's exported symbols directly at link time; an untrusted/bare payload
 * uses rst 0x08 so it needs no link-time knowledge of kernel addresses.
 */

#endif /* KERNEL_H */

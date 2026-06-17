/*
 * Declares the PartOS threading model: thread objects, the four scheduler
 * state lists, and the preemptive round-robin context switch driven by the
 * VBL tick. A thread blocked in thread_wait4events sits on the waiting list
 * and is not scheduled until one of its events is signaled.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#include "sysobj.h"
#include "evt.h"

#define THREAD_STATE_SUSPENDED      0
#define THREAD_STATE_RUNNING        1
#define THREAD_STATE_WAITING        2
#define THREAD_STATE_JOINED         3
#define THREAD_STATE_TERMINATED     4

/*
 * Saved context size: af,bc,de,hl,ix,iy,af',bc',de',hl' (20) + return (2).
 */
#define CONTEXT_SIZE                22

/*
 * Thread object. Layout must match src/kernel/thread.s.
 */
typedef struct thread_s {
    sysobj_t hdr;                       /* thread is a system object */
    uint16_t sp;                        /* saved stack pointer (context on stack) */
    uint8_t  startup[10];               /* bootstrap: call entry / ld hl,t / jp exit */
    event_t **wait;                     /* events the thread is blocked on, or NULL */
    uint8_t  num_events;                /* number of events in wait[] */
    uint8_t  state;                     /* THREAD_STATE_* */
    struct thread_s **joined;           /* joined threads (unused for now) */
    void    *process;                   /* parent process, or NULL */
} thread_t;

extern thread_t *thread_current;
extern thread_t *thread_first_suspended;
extern thread_t *thread_first_running;
extern thread_t *thread_first_waiting;
extern thread_t *thread_first_terminated;

/*
 * Create a thread (initially suspended) with its own stack; returns the thread
 * or NULL on failure.
 */
thread_t *thread_create(void (*entry_point)(void), uint16_t stack_size, void *process);

/*
 * Move a thread suspended -> running.
 */
void thread_resume(thread_t *t);

/*
 * Move a thread running -> suspended and yield.
 */
void thread_suspend(thread_t *t);

/*
 * Terminate a thread (also the bootstrap return target); never returns.
 */
void thread_exit(thread_t *t);

/*
 * Block the current thread until any of the given events is signaled.
 */
void thread_wait4events(event_t **events, uint8_t num_events);

/*
 * Pick the next thread to run (used by the scheduler tick).
 */
thread_t *_thread_select_next(void);

/*
 * VBL scheduler tick / context switch (install at the VBL IM2 vector).
 */
void _thread_robin(void);

#endif /* THREAD_H */

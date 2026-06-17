/*
 * Declares the kernel sync-event object and its operations. An event is a
 * system object that is either signaled or non-signaled and is used by the
 * scheduler to block and unblock threads.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef EVT_H
#define EVT_H

#include <stdint.h>

#include "sysobj.h"

/*
 * Event state. Stored as a single byte (see event_t.state) to match the
 * hand-written kernel layout; do not widen to an int-sized enum.
 */
typedef enum event_state_e {
    nonsignaled = 0,
    signaled    = 1
} event_state_t;

/*
 * Sync event. Layout must match src/kernel/evt.s:
 *   +0 next / +2 owner (sysobj), +4 state (1 byte).
 */
typedef struct event_s {
    sysobj_t hdr;                       /* event is a system object */
    uint8_t  state;                     /* 0 = non-signaled, 1 = signaled */
} event_t;

/*
 * Head of the event list.
 */
extern event_t *_evt_first;

/*
 * Create one non-signaled event owned by the supplied resource owner.
 */
event_t *evt_create(void *owner);

/*
 * Destroy one event and remove it from the event list.
 */
event_t *evt_destroy(event_t *e);

/*
 * Set the state of a live event. Returns the event, or NULL if the handle is
 * no longer a member of the event list.
 */
event_t *evt_set(event_t *e, uint8_t newstate);

#endif /* EVT_H */

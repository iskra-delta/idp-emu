/*
 * Declares the kernel soft-timer object and the timer-chain helper. Each
 * installed timer carries a hook and a tick reload value; the timer chain is
 * walked once per scheduler tick (50 Hz) to fire hooks that are due.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#include "sysobj.h"

/*
 * Timer period that fires on every scheduler tick.
 */
#define EVERYTIME 0

/*
 * Installed timer callback. Layout must match src/kernel/timer.s:
 *   +0 next / +2 owner (sysobj), +4 hook, +6 ticks, +8 _tick_count.
 */
typedef struct timer_s {
    sysobj_t hdr;                       /* timer is a system object */
    void (*hook)(void);                 /* hook routine */
    uint16_t ticks;                     /* reload value (fires every ticks+1) */
    uint16_t _tick_count;               /* internal countdown */
} timer_t;

/*
 * Head of the installed-timer list.
 */
extern timer_t *_tmr_first;

/*
 * Install one timer callback owned by the supplied resource owner.
 */
timer_t *tmr_install(void (*hook)(void), uint16_t ticks, void *owner);

/*
 * Remove one installed timer callback.
 */
timer_t *tmr_uninstall(timer_t *t);

/*
 * Fire all timers due on the current tick. Must be called from inside the
 * 50 Hz interrupt, which has to hold an ir_disable/ir_enable bracket.
 */
void _tmr_chain(void);

#endif /* TIMER_H */

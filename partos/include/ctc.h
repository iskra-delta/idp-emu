/*
 * Documents the Partner Z80 CTC (counter/timer) used by PartOS as the system
 * tick source. The CTC has four channels at I/O ports 0xC8..0xCB and sits
 * second in the Z80 interrupt daisy chain (DMA > CTC > SIO1 > SIO2 > PIO).
 *
 * Channel 3 is clocked by the SCN2674 AVDC vertical-blank line, so it is the
 * frame-rate (~50 Hz) "VBL" used for preemption / scheduler ticks.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef CTC_H
#define CTC_H

#include <stdint.h>

/* CTC channel I/O ports. */
#define CTC_CH0   0xC8      /* system timer (cascades to ch1) */
#define CTC_CH1   0xC9      /* counter fed by ch0 (fdc motor etc.) */
#define CTC_CH2   0xCA
#define CTC_CH3   0xCB      /* AVDC vertical blank (VBL) -> scheduler tick */

/*
 * IM2 vector base (low byte; the high byte is the I register / IM2 page).
 * A channel emits base + 2*channel, so:
 *   ch0 = 0x88, ch1 = 0x8A, ch2 = 0x8C, ch3 (VBL) = 0x8E.
 * Base must be a multiple of 8.
 */
#define CTC_VEC_BASE  0x88
#define CTC_VEC_VBL   (CTC_VEC_BASE + 6)    /* channel 3 / vertical blank */

#endif /* CTC_H */

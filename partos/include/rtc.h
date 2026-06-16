/*
 * Documents the minimal real-time clock payload exchanged through the
 * PartOS RTC device using ordinary read and write calls.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/*
 * Standard PartOS RTC payload used by the rtc device's read()/write() calls.
 *
 * Values are plain binary, not packed BCD.
 */
#define RTC_TIME_SIZE   6

typedef struct minimal_rtc_time_s {
    uint8_t sec;    /* 0-59 */
    uint8_t min;    /* 0-59 */
    uint8_t hour;   /* 0-23 */
    uint8_t mday;   /* 1-31 */
    uint8_t mon;    /* 1-12 */
    uint8_t year;   /* 0-99 */
} minimal_rtc_time_t;

#endif /* RTC_H */

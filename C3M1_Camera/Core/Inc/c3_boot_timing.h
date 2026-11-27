/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_BOOT_TIMING_H
#define C3_BOOT_TIMING_H
#include <stdint.h>
/* Unsigned elapsed is wrap-safe. Wait from power-on GPIO state rather than
 * serializing the sensor and LCD warmup delays. */
static inline uint32_t c3_boot_remaining_ms(uint32_t start,uint32_t now,uint32_t minimum)
{
    uint32_t elapsed=now-start;
    return elapsed<minimum ? minimum-elapsed : 0u;
}
#endif

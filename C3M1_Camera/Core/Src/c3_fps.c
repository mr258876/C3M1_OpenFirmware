/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_fps.h"
void c3_fps_reset(c3_fps_t *s)
{
    s->start_ms = 0u;
    s->frames = 0u;
    s->tenths = 0u;
    s->started = 0u;
}
uint8_t c3_fps_frame(c3_fps_t *s, uint32_t now_ms)
{
    if (!s->started) {
        s->start_ms = now_ms;
        s->frames = 0u;
        s->started = 1u;
        return 0u;
    }
    if (s->frames < 65535u) ++s->frames;
    uint32_t elapsed = (uint32_t)(now_ms - s->start_ms);
    if (elapsed < 1000u) return 0u;
    uint32_t tenths = ((uint32_t)s->frames * 10000u + elapsed / 2u) / elapsed;
    if (tenths > 999u) tenths = 999u;
    s->tenths = (uint16_t)tenths;
    s->frames = 0u;
    s->start_ms = now_ms;
    return 1u;
}

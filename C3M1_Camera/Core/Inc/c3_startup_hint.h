/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_STARTUP_HINT_H
#define C3_STARTUP_HINT_H
#include <stdint.h>
#define C3_STARTUP_HINT_MS 5000u
#define C3_STARTUP_HINT_KEY "HOLD FILM LEVER TO OPEN FUNCTION MENU"
typedef struct {uint32_t first_ms;uint8_t state;} c3_startup_hint_t;
/* Arm after successful setup; the first complete preview frame starts the clock. */
static inline void c3_startup_hint_arm(c3_startup_hint_t *s){s->first_ms=0u;s->state=0u;}
static inline void c3_startup_hint_dismiss(c3_startup_hint_t *s){s->state=2u;}
static inline uint8_t c3_startup_hint_frame(c3_startup_hint_t *s,uint8_t enabled,uint8_t frame_ok,uint32_t now)
{
    if(!enabled){c3_startup_hint_dismiss(s);return 0u;}
    if(s->state==2u)return 0u;
    if(!s->state){if(!frame_ok)return 0u;s->first_ms=now;s->state=1u;}
    if((uint32_t)(now-s->first_ms)>=C3_STARTUP_HINT_MS){c3_startup_hint_dismiss(s);return 0u;}
    return frame_ok;
}
#endif

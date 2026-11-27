/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_INPUT_H
#define C3_INPUT_H
#include <stdint.h>
#include "c3_buttons.h"

/* Sample keys independently of the camera FPS. At 5ms/scan, 20ms debounce,
 * a 70-100ms press can be seen even if foreground takes 200ms per frame.
 * The ISR only records events; all UI and camera operations remain in main.
 */
typedef struct {
    c3_button_events_t events;
    uint16_t stable;
    uint16_t film_chords; /* ISO/4X pressed at any point while FILM was held */
} c3_input_batch_t;
void c3_input_reset(uint16_t pressed_mask, uint32_t now_ms);
/* Also available to host tests. ISR is the only runtime caller. */
void c3_input_sample(uint16_t pressed_mask, uint32_t now_ms);
c3_input_batch_t c3_input_take(void);
void c3_input_systick(void);
#endif

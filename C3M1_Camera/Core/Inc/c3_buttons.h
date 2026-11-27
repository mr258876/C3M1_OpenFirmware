/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_BUTTONS_H
#define C3_BUTTONS_H
#include <stdint.h>
/* Input already converted to active-high pressed bits. Defer short actions
 * until RELEASE so a long-press cannot also trigger the short action.
 * SHUTTER uses the separate press event to keep shutter response quick.
 */
#define C3_BUTTON_DEBOUNCE_MS 20u
#define C3_BUTTON_HOLD_MS 650u
typedef struct {
    uint32_t change_ms;
    uint32_t press_ms;
    uint8_t raw;
    uint8_t stable;
    uint8_t long_fired;
} c3_button_bit_t;
typedef struct {
    c3_button_bit_t b[4];
} c3_buttons_t;
typedef struct {
    uint16_t pressed;
    uint16_t released;       /* all debounced key-up edges */
    uint16_t short_released;
    uint16_t long_pressed;
    uint16_t long_released;  /* release after long hold (no extra action) */
} c3_button_events_t;
/* Production board uses PF1 ISO, PF2 SHUTTER, PF3 4X, PF10 FILM. */
void c3_buttons_reset(c3_buttons_t *s, uint16_t pressed_mask, uint32_t now_ms);
uint16_t c3_buttons_stable_mask(const c3_buttons_t *s);
c3_button_events_t c3_buttons_poll(c3_buttons_t *s,
                                    uint16_t pressed_mask, uint32_t now_ms);
#endif

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_buttons.h"
#include "c3_board_pins.h"

static const uint16_t button_pin[4] = {C3_BTN_ISO, C3_BTN_SHUTTER, C3_BTN_4X, C3_BTN_FILM};
void c3_buttons_reset(c3_buttons_t *s, uint16_t pressed_mask, uint32_t now_ms)
{
    for (unsigned i = 0; i < 4u; ++i) {
        c3_button_bit_t *b = &s->b[i];
        b->raw = (uint8_t)!!(pressed_mask & button_pin[i]);
        b->stable = b->raw;
        b->change_ms = now_ms;
        b->press_ms = now_ms;
        b->long_fired = 0u;
    }
}
c3_button_events_t c3_buttons_poll(c3_buttons_t *s,
                                    uint16_t pressed_mask, uint32_t now_ms)
{
    c3_button_events_t events = {0u,0u,0u,0u,0u};
    for (unsigned i = 0u; i < 4u; ++i) {
        c3_button_bit_t *b = &s->b[i];
        uint16_t pin = button_pin[i];
        uint8_t raw = (uint8_t)!!(pressed_mask & pin);
        if (raw != b->raw) {
            b->raw = raw;
            b->change_ms = now_ms;
        }
        if (raw != b->stable &&
            (uint32_t)(now_ms - b->change_ms) >= C3_BUTTON_DEBOUNCE_MS) {
            b->stable = raw;
            if (raw) {
                b->press_ms = now_ms;
                b->long_fired = 0u;
                events.pressed |= pin;
            } else {
                events.released |= pin;
                if (b->long_fired) events.long_released |= pin;
                if (!b->long_fired) {
                    /* Even if polling was slow and missed the long hold,
                     * classify by duration, not just the last sampled tick. */
                    if (i != 1u &&
                        (uint32_t)(now_ms - b->press_ms) >= C3_BUTTON_HOLD_MS)
                        { events.long_pressed |= pin; events.long_released |= pin; }
                    else
                        events.short_released |= pin;
                }
            }
        }
        if (i != 1u && b->stable && raw && !b->long_fired &&
            (uint32_t)(now_ms - b->press_ms) >= C3_BUTTON_HOLD_MS) {
            b->long_fired = 1u;
            events.long_pressed |= pin;
        }
    }
    return events;
}

uint16_t c3_buttons_stable_mask(const c3_buttons_t *s)
{
    uint16_t mask = 0u;
    for (unsigned i = 0u; i < 4u; ++i)
        if (s->b[i].stable) mask |= button_pin[i];
    return mask;
}

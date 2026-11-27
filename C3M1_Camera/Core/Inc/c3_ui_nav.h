/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_UI_NAV_H
#define C3_UI_NAV_H
#include <stdint.h>
#include "c3_buttons.h"
#include "c3_settings.h"
#include "c3_playback.h"

/* Hold FILM for >=650 ms to enter the EV page. Rotate TSR16 to
 * adjust nominal exposure compensation in half EVs; ISO opens playback
 * when C3_PLAYBACK_ENABLE is on (otherwise ignored),
 * 4X opens SETTINGS. Both pages remain open after FILM release.
 * SHOT exits either view without a photo.
 * A short tap of FILM does not open a page or toggle OSD. */
typedef enum {
    C3_UI_PREVIEW=0, C3_UI_MODE=1, C3_UI_MENU=2, C3_UI_PLAY=3, C3_UI_CONFIRM=4, C3_UI_ABOUT=5, C3_UI_LANGUAGE=6
} c3_ui_page_t;
enum {
    C3_NAV_NONE=0,
    C3_NAV_MODE_OPEN=1u<<0,
    C3_NAV_MENU_OPEN=1u<<1,
    C3_NAV_BACK=1u<<2,
    C3_NAV_PLAY_OPEN=1u<<3,
    C3_NAV_CURSOR=1u<<4,
    C3_NAV_SETTING=1u<<5,
    C3_NAV_EV_ADJUST=1u<<6,
    C3_NAV_PLAY_STEP=1u<<7,
    C3_NAV_CONFIRM_OPEN=1u<<8, C3_NAV_CONFIRM_CANCEL=1u<<9,
    C3_NAV_RESET=1u<<10, C3_NAV_FORMAT=1u<<11,
    C3_NAV_ABOUT_OPEN=1u<<12, C3_NAV_ABOUT_BACK=1u<<13,
    C3_NAV_LANGUAGE_OPEN=1u<<14, C3_NAV_LANGUAGE_CONFIRM=1u<<15
};
#define C3_UI_DIAL_SETTLE_MS 60u
/* Set 1 at build time if the physical clockwise direction feels reversed. */

typedef struct {
    c3_ui_page_t page;
    uint8_t selected;
    uint8_t first_visible;
    uint8_t dial_stable;
    uint8_t dial_candidate;
    uint32_t dial_changed_ms;
    uint8_t inhibit_film;
    uint8_t confirm_armed;
    uint8_t language_choice; /* 0 English, 1 simplified Chinese */
    uint8_t language_required;
    int8_t ev_step_delta; /* MODE EV delta or PLAY signed dial movement */
} c3_ui_nav_t;
void c3_ui_nav_reset(c3_ui_nav_t *s, uint8_t raw_dial, uint32_t now_ms);
void c3_ui_nav_language(c3_ui_nav_t *s, uint8_t raw_dial, uint32_t now_ms,
                        uint8_t choice, uint8_t required);
uint16_t c3_ui_nav_poll(c3_ui_nav_t *s, c3_button_events_t keys,
                       uint16_t stable_pressed, uint8_t raw_dial,
                       uint32_t now_ms, c3_settings_t *settings);
#endif

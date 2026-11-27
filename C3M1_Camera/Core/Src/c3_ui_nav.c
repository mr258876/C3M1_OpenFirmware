#include "c3_ui_nav.h"
#include "c3_board_pins.h"
#define SHOT C3_BTN_SHUTTER
#define ZOOM C3_BTN_4X
#define FILM C3_BTN_FILM
#define VISIBLE_ROWS 7u
void c3_ui_nav_reset(c3_ui_nav_t *s, uint8_t raw_dial, uint32_t now_ms)
{
    s->page = C3_UI_PREVIEW;
    s->selected = 0u;
    s->first_visible = 0u;
    s->dial_stable = raw_dial & 15u;
    s->dial_candidate = s->dial_stable;
    s->dial_changed_ms = now_ms;
    s->inhibit_film = 0u;
    s->confirm_armed = 0u;
#if C3_LANGUAGE_SETUP
    s->language_choice = 0u;
    s->language_required = 0u;
#endif
    s->ev_step_delta = 0;
}
#if C3_LANGUAGE_SETUP
void c3_ui_nav_language(c3_ui_nav_t *s, uint8_t raw_dial, uint32_t now_ms,
                        uint8_t choice, uint8_t required)
{
    c3_ui_nav_reset(s,raw_dial,now_ms);
    s->page=C3_UI_LANGUAGE;
    s->language_choice=!!choice;
    s->language_required=!!required;
}
#endif
static int poll_rotary_delta(c3_ui_nav_t *s, uint8_t raw_dial,
                             uint32_t now_ms)
{
    raw_dial &= 15u;
    if (raw_dial != s->dial_candidate) {
        s->dial_candidate = raw_dial;
        s->dial_changed_ms = now_ms;
    }
    if (raw_dial == s->dial_stable ||
        (uint32_t)(now_ms-s->dial_changed_ms) < C3_UI_DIAL_SETTLE_MS)
        return 0;
    uint8_t forward = (uint8_t)((raw_dial - s->dial_stable) & 15u);
    s->dial_stable = raw_dial;
    if (forward == 8u) return 0; /* exactly opposite: ambiguous */
    int step = (forward <= 7u) ? (int)forward : (int)forward-16;
    if (step > 4) step=4; /* bounce/skipped contacts */
    if (step < -4) step=-4;
#if C3_UI_REVERSE_DIAL
    step = -step;
#endif
    return step;
}
static void track_selection(c3_ui_nav_t *s, int steps)
{
    int n = ((int)s->selected + steps) % C3_SETTINGS_COUNT;
    if (n < 0) n += C3_SETTINGS_COUNT;
    s->selected = (uint8_t)n;
    if (s->selected < s->first_visible) s->first_visible = s->selected;
    if (s->selected >= s->first_visible + VISIBLE_ROWS)
        s->first_visible = (uint8_t)(s->selected - VISIBLE_ROWS + 1u);
}
uint16_t c3_ui_nav_poll(c3_ui_nav_t *s, c3_button_events_t keys,
                       uint16_t stable_pressed, uint8_t raw_dial,
                       uint32_t now_ms, c3_settings_t *settings)
{
    uint16_t a = C3_NAV_NONE;
    s->ev_step_delta = 0;
    uint8_t film_was_inhibited = s->inhibit_film;
    if (!(stable_pressed & FILM)) s->inhibit_film = 0u;
#if C3_LANGUAGE_SETUP
    if (s->page == C3_UI_LANGUAGE) {
        /* Both entry and failed writes require a fresh unchorded press.
         * SHOT can cancel only an optional language change. The caller
         * leaves this page only after a successful Flash commit. */
        if (keys.pressed & SHOT) {
            s->confirm_armed=0u;
            if (!s->language_required) return C3_NAV_CONFIRM_CANCEL;
            return C3_NAV_NONE;
        }
        if (!stable_pressed && !keys.pressed) s->confirm_armed=1u;
        int movement=poll_rotary_delta(s,raw_dial,now_ms);
        if (movement) {
            s->language_choice=(uint8_t)((s->language_choice+movement+4)%2);
            a |= C3_NAV_CURSOR;
        }
        if (s->confirm_armed && keys.pressed==ZOOM && stable_pressed==ZOOM) {
            s->confirm_armed=0u;
            return a | C3_NAV_LANGUAGE_CONFIRM;
        }
        if (stable_pressed || keys.pressed) s->confirm_armed=0u;
        return a;
    }
#endif
    if (s->page == C3_UI_PREVIEW) {
        /* Do not open MODE on a short tap. A chord pressed while the lever
         * is still below 650ms must not hijack ISO gain or preview zoom.
         * If the 4X key is already held at the long-press threshold, opening
         * SETTINGS directly avoids requiring a release/re-press. */
        if ((keys.long_pressed & FILM) && !film_was_inhibited) {
            if ((stable_pressed & ZOOM) || (keys.pressed & ZOOM)) {
                s->page = C3_UI_MENU;
                s->dial_stable = (uint8_t)(raw_dial & 15u);
                s->dial_candidate = s->dial_stable;
                s->dial_changed_ms = now_ms;
                a |= C3_NAV_MENU_OPEN;
            } else {
                s->page = C3_UI_MODE;
                s->dial_stable = (uint8_t)(raw_dial & 15u);
                s->dial_candidate = s->dial_stable;
                s->dial_changed_ms = now_ms;
                a |= C3_NAV_MODE_OPEN;
            }
        }
        return a;
    }
    if (s->page == C3_UI_MODE) {
        if (keys.pressed & SHOT) {
            s->page = C3_UI_PREVIEW;
            s->inhibit_film = (uint8_t)!!(stable_pressed & FILM);
            return C3_NAV_BACK;
        }
        if (keys.pressed & ZOOM) {
            s->page = C3_UI_MENU;
            s->dial_stable = (uint8_t)(raw_dial & 15u);
            s->dial_candidate = s->dial_stable;
            s->dial_changed_ms = now_ms;
            return C3_NAV_MENU_OPEN;
        }
#if C3_PLAYBACK_ENABLE
        if (keys.pressed & C3_BTN_ISO) {
            s->page = C3_UI_PLAY;
            s->dial_stable = s->dial_candidate = (uint8_t)(raw_dial & 15u);
            s->dial_changed_ms = now_ms;
            return C3_NAV_PLAY_OPEN;
        }
#endif
        int movement = poll_rotary_delta(s, raw_dial, now_ms);
        if (movement) {
            /* MODE EV direction is opposite the old mapping; leave SETTINGS scroll unchanged. */
            s->ev_step_delta = (int8_t)-movement;
            a |= C3_NAV_EV_ADJUST;
        }
        return a;
    }
#if C3_PLAYBACK_ENABLE
    if (s->page == C3_UI_PLAY) {
        if (keys.pressed & SHOT) {
            s->page = C3_UI_PREVIEW;
            s->inhibit_film = (uint8_t)!!(stable_pressed & FILM);
            return C3_NAV_BACK;
        }
        int movement = poll_rotary_delta(s,raw_dial,now_ms);
        if (movement) {
            s->ev_step_delta=(int8_t)movement;
            return C3_NAV_PLAY_STEP;
        }
        return C3_NAV_NONE;
    }
#endif
#if C3_MENU_ACTIONS
    if (s->page == C3_UI_CONFIRM) {
        /* Fresh ISO press after all keys released; shutter takes precedence. */
        if (keys.pressed & SHOT) {
            s->page=C3_UI_MENU; s->confirm_armed=0u;
            s->dial_stable=s->dial_candidate=(uint8_t)(raw_dial&15u);
            s->dial_changed_ms=now_ms;
            return C3_NAV_CONFIRM_CANCEL;
        }
        if (!stable_pressed && !keys.pressed) s->confirm_armed=1u;
        if (s->confirm_armed && keys.pressed==C3_BTN_ISO && stable_pressed==C3_BTN_ISO) {
            s->page=C3_UI_MENU; s->confirm_armed=0u;
            s->dial_stable=s->dial_candidate=(uint8_t)(raw_dial&15u);
            s->dial_changed_ms=now_ms;
            return s->selected==C3_SETTING_RESET ? C3_NAV_RESET :
                   s->selected==C3_SETTING_FORMAT ? C3_NAV_FORMAT : C3_NAV_CONFIRM_CANCEL;
        }
        if (stable_pressed || keys.pressed) s->confirm_armed=0u;
        return C3_NAV_NONE;
    }
#endif
#if C3_FUNCTION_MENU
    if (s->page == C3_UI_ABOUT) {
        if (keys.pressed & SHOT) {
            s->page=C3_UI_MENU;
            s->dial_stable=s->dial_candidate=(uint8_t)(raw_dial&15u);
            s->dial_changed_ms=now_ms;
            return C3_NAV_ABOUT_BACK;
        }
        return C3_NAV_NONE;
    }
#endif
    /* MENU: shutter always exits. The 4X down edge changes the highlighted
     * setting; it cannot also trigger monochrome from the preview shortcut. */
    if (keys.pressed & SHOT) {
        s->page = C3_UI_PREVIEW;
        s->inhibit_film = (uint8_t)!!(stable_pressed & FILM);
        return C3_NAV_BACK;
    }
    if ((keys.pressed & ZOOM) && settings) {
#if C3_LANGUAGE_SETUP
        if (s->selected==C3_SETTING_LANGUAGE) return C3_NAV_LANGUAGE_OPEN;
#endif
#if C3_MENU_ACTIONS
        if (s->selected==C3_SETTING_RESET || s->selected==C3_SETTING_FORMAT) {
            s->page=C3_UI_CONFIRM; s->confirm_armed=0u;
            return C3_NAV_CONFIRM_OPEN;
        }
#endif
#if C3_FUNCTION_MENU
        if (s->selected==C3_SETTING_ABOUT) {
            s->page=C3_UI_ABOUT;
            return C3_NAV_ABOUT_OPEN;
        }
#else
        if (s->selected == C3_SETTING_BATTERY) {
            a |= C3_NAV_CURSOR; /* Refresh reading without saving Flash. */
        } else
#endif
        {
            c3_settings_next(settings,s->selected);
            a |= C3_NAV_SETTING;
        }
    }
    int movement = poll_rotary_delta(s, raw_dial, now_ms);
    if (movement) {
        track_selection(s, movement);
        a |= C3_NAV_CURSOR;
    }
    return a;
}

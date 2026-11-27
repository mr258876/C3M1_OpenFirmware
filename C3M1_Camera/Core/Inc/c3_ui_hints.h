/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_UI_HINTS_H
#define C3_UI_HINTS_H
#include <stdint.h>
typedef enum {
    C3_HINT_DIAL, C3_HINT_ISO, C3_HINT_4X, C3_HINT_SHOT, C3_HINT_FILM
} c3_hint_key_t;
/* Native-pixel 16-high hint cell. Action is a translatable text key.
 * All icons are drawn only on static menu/recovery screens. */
void c3_ui_hint(int x,int y,c3_hint_key_t key,const char *action,uint16_t color);
#endif

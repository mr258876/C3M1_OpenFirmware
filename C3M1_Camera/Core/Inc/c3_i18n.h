#ifndef C3_I18N_H
#define C3_I18N_H
#include <stdint.h>
#include "c3_lcd.h"
void c3_i18n_set_language(uint8_t chinese);
uint8_t c3_i18n_is_chinese(void);
const char *c3_i18n_translate(const char *key);
/* Layout is in LCD pixels; CJK glyphs use a fixed 16px cell. ASCII keeps
 * the legacy scale. Only static menus/messages use this path. */
int c3_i18n_width(const char *text,int scale);
void c3_ui_text(int x,int y,const char *key,uint16_t color,int scale);
void c3_ui_message(const char *key);
/* Opaque native-pixel boot hint, one LCD address window, no frame buffer. */
void c3_ui_draw_startup_hint(void);
#endif

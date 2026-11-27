#ifndef C3_MENU_H
#define C3_MENU_H
#include "c3_settings.h"
#include "c3_ui_nav.h"
#include "c3_exposure.h"
void c3_menu_draw_mode(const c3_exposure_t *exposure);
void c3_menu_draw_settings(const c3_ui_nav_t *nav,
                           const c3_settings_t *settings);
#if C3_LANGUAGE_SETUP
void c3_menu_draw_language(uint8_t choice, uint8_t required, uint8_t save_error);
#endif
#if C3_FUNCTION_MENU
void c3_menu_draw_battery(void);
void c3_menu_draw_about(void);
#endif
#if C3_MENU_ACTIONS
void c3_menu_draw_confirm(uint8_t item);
void c3_menu_draw_format_status(const char *status, uint8_t busy);
#endif
#endif

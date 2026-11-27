#include "c3_menu.h"
#include "c3_lcd.h"
#include "c3_i18n.h"
#include "c3_ui_hints.h"
#include "c3_battery.h"
#include "c3_font.h"
#include <string.h>

#define BG 0x0841u
#define PANEL 0x1082u
#define ACTIVE 0x1B47u
#define CYAN 0x07FFu
#define WHITE 0xFFFFu
#define MUTED 0xA534u

static void title(const char *s)
{
    c3_lcd_fill(BG);
    c3_lcd_rect(0,0,320,31,PANEL);
    c3_ui_text(12,8,s,WHITE,2);
    c3_lcd_rect(0,31,320,2,CYAN);
}

#if C3_FUNCTION_MENU
/* Only repaint the reserved header cell, never the live OSD or menu body. */
void c3_menu_draw_battery(void)
{
    char value[16];
    uint16_t mv;
    if (c3_battery_read_mv(&mv)) c3_battery_format(value,mv);
    else c3_battery_format_unknown(value);
#if C3_I18N
    int width=c3_i18n_width(c3_i18n_translate(value),2);
#else
    int width=(int)strlen(value)*12;
#endif
    c3_lcd_rect(174,0,146,31,PANEL);
    c3_ui_text(308-width,8,value,WHITE,2);
}

#if C3_STARTUP_HINT
/* 5x7 lowercase subset for the project wordmark only. Legacy OSD and
 * menu glyphs remain uppercase, and no font license changes are needed. */
static void draw_project_name(void)
{
    static const struct {char ch; uint8_t cols[5];} lower[]={
        {'a',{0x20,0x54,0x54,0x54,0x78}},
        {'e',{0x38,0x54,0x54,0x54,0x18}},
        {'i',{0,0x44,0x7D,0x40,0}},
        {'m',{0x7C,0x04,0x18,0x04,0x78}},
        {'n',{0x7C,0x08,0x04,0x04,0x78}},
        {'p',{0x7C,0x14,0x14,0x14,0x08}},
        {'r',{0x7C,0x08,0x04,0x04,0x08}},
        {'w',{0x3C,0x40,0x30,0x40,0x3C}}
    };
    const char *s="C3M1 OpenFirmware";
    int x=16;
    for(;*s;s++,x+=12){
        const uint8_t *f=c3_font_glyph(*s);
        for(unsigned i=0;i<sizeof lower/sizeof lower[0];i++)
            if(lower[i].ch==*s){f=lower[i].cols;break;}
        if(f) for(int c=0;c<5;c++) for(int r=0;r<7;r++)
            if(f[c]&(1u<<r))c3_lcd_rect(x+2*c,48+2*r,2,2,CYAN);
    }
}
#endif
void c3_menu_draw_about(void)
{
    title("ABOUT");
#if C3_STARTUP_HINT
    draw_project_name();
#else
    c3_ui_text(16,48,"C3M1",CYAN,3);
#endif
    static const char *const rows[][2]={
#if C3_POSTSHOT_KEEP_BG
        {"FIRMWARE", "v0.2.21"},
#elif C3_POSTSHOT_IMMEDIATE
        {"FIRMWARE", "v0.2.20"},
#elif C3_POSTSHOT
        {"FIRMWARE", "v0.2.19"},
#elif C3_DEFAULTS_V218
        {"FIRMWARE", "v0.2.18"},
#elif C3_LANGUAGE_LAYOUT
        {"FIRMWARE", "v0.2.17"},
#elif C3_LANGUAGE_SETUP
        {"FIRMWARE", "v0.2.16"},
#elif C3_STARTUP_HINT
        {"FIRMWARE", "v0.2.15"},
#else
        {"FIRMWARE", "v0.2.14"},
#endif
        {"BUILD", "ACR8 F2"},
        {"PROCESSOR", "STM32F103ZET6"},
        {"SENSOR", "OV7725"},
        {"DISPLAY", "320x240"}
    };
    for (unsigned i=0;i<sizeof(rows)/sizeof(rows[0]);i++) {
        int y=88+(int)i*23;
        c3_ui_text(16,y,rows[i][0],MUTED,1);
        c3_ui_text(144,y,rows[i][1],WHITE,1);
    }
    c3_lcd_rect(0,214,320,26,PANEL);
#if C3_UI_ICONS
    c3_ui_hint(10,219,C3_HINT_SHOT,"BACK",WHITE);
#else
    c3_ui_text(10,223,"SHOT - BACK",WHITE,1);
#endif
}
#endif

void c3_menu_draw_mode(const c3_exposure_t *exposure)
{
    if (!exposure) return;
#if C3_FUNCTION_MENU
    title("FUNCTION MENU");
    c3_menu_draw_battery();
#else
    title("EXPOSURE COMP");
#endif
    char ev[7];
    c3_exposure_ev_label(ev,exposure->ev_half_steps);
    c3_ui_text(18,46,"EV TARGET",MUTED,1);
    c3_ui_text(89,66,ev,CYAN,4);
    /* Thirteen half-EV positions; one active cell. */
    for (unsigned i=0;i<13u;++i) {
        int x=17+(int)i*22;
        c3_lcd_rect(x,111,18,12,i==(unsigned)(exposure->ev_half_steps+6)?CYAN:PANEL);
    }
    c3_ui_text(17,130,"-3",MUTED,1);
    c3_ui_text(155,130,"0",MUTED,1);
    c3_ui_text(285,130,"+3",MUTED,1);
    c3_ui_text(14,153,c3_exposure_ev_active(exposure)?"EV COMP. IS APPROXIMATE":"M MODE - EV INACTIVE",MUTED,1);
#if C3_UI_ICONS
    c3_ui_hint(14,173,C3_HINT_DIAL,"ADJUST EV",WHITE);
    c3_ui_hint(160,173,C3_HINT_4X,"SETTINGS",WHITE);
#if C3_PLAYBACK_ENABLE
    c3_ui_hint(14,193,C3_HINT_ISO,"PLAY",WHITE);
    c3_ui_hint(160,193,C3_HINT_SHOT,"BACK",WHITE);
#else
    c3_ui_hint(14,193,C3_HINT_SHOT,"BACK",WHITE);
#endif
#else
    c3_ui_text(14,177,"DIAL - EV    4X - SETTINGS",WHITE,1);
#if C3_PLAYBACK_ENABLE
    c3_ui_text(14,197,"ISO - PLAY   SHOT - BACK",WHITE,1);
#else
    c3_ui_text(14,197,"SHOT - BACK",WHITE,1);
#endif
#endif
    c3_ui_text(14,217,"MAY NEED TO RESET SHUTTER SPEED",MUTED,1);
}

void c3_menu_draw_settings(const c3_ui_nav_t *nav,
                           const c3_settings_t *settings)
{
    if (!nav || !settings) return;
#if C3_I18N
    c3_i18n_set_language(c3_settings_language(settings));
#endif
    title("SETTINGS");
    for (uint8_t row=0u;row<7u;row++) {
        uint8_t item = (uint8_t)(nav->first_visible+row);
        if (item >= C3_SETTINGS_COUNT) break;
        int y = 38 + (int)row*24;
        uint8_t selected = (uint8_t)(item==nav->selected);
        c3_lcd_rect(10,y,300,23,selected?ACTIVE:PANEL);
        if (selected) c3_lcd_rect(10,y,3,23,CYAN);
        c3_ui_text(23,y+8,c3_settings_label(item),
                    selected?WHITE:MUTED,1);
#if !C3_FUNCTION_MENU
        if (item == C3_SETTING_BATTERY) {
            char value[16];
            uint16_t mv;
            if (c3_battery_read_mv(&mv)) c3_battery_format(value,mv);
            else c3_battery_format_unknown(value);
            c3_ui_text(230,y+8,value,selected?CYAN:WHITE,1);
        } else
#endif
        {
            c3_ui_text(240,y+8,c3_settings_value(settings,item),
                        selected?CYAN:WHITE,1);
        }
    }
    c3_lcd_rect(0,214,320,26,PANEL);
#if C3_UI_ICONS
    c3_ui_hint(10,219,C3_HINT_DIAL,"MOVE",WHITE);
#if C3_FUNCTION_MENU
    c3_ui_hint(111,219,C3_HINT_4X,(nav->selected==C3_SETTING_ABOUT
#if C3_LANGUAGE_SETUP
               || nav->selected==C3_SETTING_LANGUAGE
#endif
               )?"OPEN":"SET",WHITE);
#else
    c3_ui_hint(111,219,C3_HINT_4X,nav->selected==C3_SETTING_BATTERY?"REFRESH":"SET",WHITE);
#endif
    c3_ui_hint(222,219,C3_HINT_SHOT,"EXIT",WHITE);
#else
#if C3_FUNCTION_MENU
    c3_ui_text(10,223,"DIAL MOVE  4X SET  SHOT EXIT",WHITE,1);
#else
#if C3_I18N
    if (c3_i18n_is_chinese()) {
        c3_ui_text(10,223,nav->selected == C3_SETTING_BATTERY ?
                   "DIAL MOVE  4X REFRESH  SHOT EXIT" : "DIAL MOVE  4X SET  SHOT EXIT",WHITE,1);
        return;
    }
#endif
    c3_ui_text(10,219,nav->selected == C3_SETTING_BATTERY ?
                "DIAL MOVE  4X REFRESH" : "DIAL MOVE  4X SET",WHITE,1);
    c3_ui_text(208,229,"SHOT EXIT",CYAN,1);
#endif
#endif
}

#if C3_MENU_ACTIONS
void c3_menu_draw_confirm(uint8_t item)
{
    uint8_t format=(uint8_t)(item==C3_SETTING_FORMAT);
    title(format?"FORMAT CARD":"RESET SETTINGS");
    c3_ui_text(16,52,format?"ALL CARD FILES WILL BE LOST":"RESTORE DEFAULT SETTINGS",0xF800u,1);
    c3_ui_text(16,78,format?"BACK UP BEFORE FORMATTING":"PHOTOS WILL NOT BE DELETED",WHITE,1);
    c3_ui_text(16,104,format?"FAT ONLY, UP TO 32 GIB":"EV AND ISO ALSO RESET",MUTED,1);
    if(format)c3_ui_text(16,130,"DO NOT REMOVE CARD OR POWER OFF",WHITE,1);
    c3_ui_hint(16,180,C3_HINT_ISO,"CONFIRM",CYAN);
    c3_ui_hint(170,180,C3_HINT_SHOT,"CANCEL",WHITE);
}
void c3_menu_draw_format_status(const char *status, uint8_t busy)
{
    title("FORMAT CARD");
    c3_ui_message(status);
    if (busy) c3_ui_text(16,160,"DO NOT REMOVE CARD OR POWER OFF",WHITE,1);
}

#endif

#if C3_LANGUAGE_SETUP
/* The highlighted language is used only for rendering this picker.
 * Restore the application language before returning; only confirmation
 * and successful Flash commit change the active language. */
void c3_menu_draw_language(uint8_t choice, uint8_t required, uint8_t save_error)
{
    uint8_t old=c3_i18n_is_chinese();
    c3_i18n_set_language(0u);
    title("LANGUAGE");
#if C3_LANGUAGE_LAYOUT
    c3_i18n_set_language(choice);
    /* English anchor stays fixed; avoid repeating an identical label. */
    if(choice!=0u)
        c3_ui_text(132,8,"LANGUAGE",WHITE,2);
#else
    c3_i18n_set_language(1u);
    c3_ui_text(132,8,"LANGUAGE",WHITE,1);
#endif
    for (uint8_t i=0;i<2u;i++) {
        int y=64+46*i;
        uint8_t selected=(uint8_t)(choice==i);
        c3_lcd_rect(18,y,284,38,selected?ACTIVE:PANEL);
        if(selected)c3_lcd_rect(18,y,3,38,CYAN);
        c3_i18n_set_language(i);
        const char *label=i?"SIMPLIFIED CHINESE":"ENGLISH";
        c3_ui_text(36,y+12,label,selected?WHITE:MUTED,2);
    }
#if C3_LANGUAGE_LAYOUT
    c3_i18n_set_language(choice);
    if(save_error || required) {
        const char *key=save_error?"SET SAVE ERROR":"SELECT LANGUAGE TO CONTINUE";
        uint16_t color=save_error?0xF800u:MUTED;
        c3_ui_text(18,168,key,color,1);
    }
    c3_lcd_rect(0,208,320,32,PANEL);
    /* Draw MOVE once in English, then append the highlighted locale's
     * translation only if different. Confirmation/cancellation are local. */
    c3_i18n_set_language(0u);
    c3_ui_hint(8,216,C3_HINT_DIAL,"MOVE",WHITE);
    c3_i18n_set_language(choice);
    if(choice!=0u)
        c3_ui_text(58,220,"MOVE",WHITE,1);
    c3_ui_hint(110,216,C3_HINT_4X,"CONFIRM",CYAN);
    if(!required)c3_ui_hint(222,216,C3_HINT_SHOT,"CANCEL",WHITE);
#else
    c3_i18n_set_language(0u);
    if(save_error || required) {
        const char *key=save_error?"SET SAVE ERROR":"SELECT LANGUAGE TO CONTINUE";
        uint16_t color=save_error?0xF800u:MUTED;
        c3_ui_text(18,160,key,color,1);
        c3_i18n_set_language(1u);
        c3_ui_text(18,177,key,color,1);
        c3_i18n_set_language(0u);
    }
    c3_lcd_rect(0,208,320,32,PANEL);
    c3_ui_hint(8,216,C3_HINT_DIAL,"MOVE",WHITE);
    c3_ui_hint(110,216,C3_HINT_4X,"CONFIRM",CYAN);
    if(!required)c3_ui_hint(222,216,C3_HINT_SHOT,"CANCEL",WHITE);
    c3_i18n_set_language(1u);
    c3_ui_text(57,220,"MOVE",WHITE,1);
    c3_ui_text(181,220,"CONFIRM",CYAN,1);
    if(!required)c3_ui_text(280,220,"CANCEL",WHITE,1);
#endif
    c3_i18n_set_language(old);
}
#endif

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
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

/* Only repaint the reserved header cell, never the live OSD or menu body. */
void c3_menu_draw_battery(void)
{
    char value[16];
    uint16_t mv;
    if (c3_battery_read_mv(&mv)) c3_battery_format(value,mv);
    else c3_battery_format_unknown(value);
    int width=c3_i18n_width(c3_i18n_translate(value),2);
    c3_lcd_rect(174,0,146,31,PANEL);
    c3_ui_text(308-width,8,value,WHITE,2);
}

/* Case-sensitive ASCII for the project identity; live OSD keeps its
 * existing uppercase glyphs. The URL also requires an underscore. */
static const uint8_t *identity_glyph(char c)
{
    static const struct {char ch; uint8_t cols[5];} lower[]={
        {'a',{0x20,0x54,0x54,0x54,0x78}},
        {'b',{0x7F,0x48,0x44,0x44,0x38}},
        {'c',{0x38,0x44,0x44,0x44,0x28}},
        {'e',{0x38,0x54,0x54,0x54,0x18}},
        {'g',{0x08,0x54,0x54,0x54,0x3C}},
        {'h',{0x7F,0x08,0x04,0x04,0x78}},
        {'i',{0,0x44,0x7D,0x40,0}},
        {'l',{0,0x41,0x7F,0x40,0}},
        {'m',{0x7C,0x04,0x18,0x04,0x78}},
        {'n',{0x7C,0x08,0x04,0x04,0x78}},
        {'o',{0x38,0x44,0x44,0x44,0x38}},
        {'p',{0x7C,0x14,0x14,0x14,0x08}},
        {'r',{0x7C,0x08,0x04,0x04,0x08}},
        {'s',{0x48,0x54,0x54,0x54,0x24}},
        {'t',{0x04,0x3F,0x44,0x40,0x20}},
        {'u',{0x3C,0x40,0x40,0x20,0x7C}},
        {'v',{0x1C,0x20,0x40,0x20,0x1C}},
        {'w',{0x3C,0x40,0x30,0x40,0x3C}},
        {'y',{0x0C,0x50,0x50,0x50,0x3C}},
        {'_',{0x40,0x40,0x40,0x40,0x40}}
    };
    for(unsigned i=0;i<sizeof lower/sizeof lower[0];i++)
        if(lower[i].ch==c)return lower[i].cols;
    return c3_font_glyph(c);
}
static void identity_line(int y,const char *s,uint16_t color,int scale)
{
    int x=(320-(int)strlen(s)*6*scale)/2;
    for(;*s;s++,x+=6*scale){
        const uint8_t *f=identity_glyph(*s);
        if(!f)continue;
        for(int r=0;r<7;r++){
            int c=0;
            while(c<5){
                while(c<5&&!(f[c]&(1u<<r)))c++;
                int first=c;
                while(c<5&&(f[c]&(1u<<r)))c++;
                if(c>first)c3_lcd_rect(x+first*scale,y+r*scale,
                                      (c-first)*scale,scale,color);
            }
        }
    }
}
void c3_menu_draw_about(void)
{
    title("ABOUT");
    identity_line(45,"C3M1 OpenFirmware",CYAN,2);
    identity_line(72,"v0.2.21",WHITE,2);
    identity_line(99,"2026 mr258876",WHITE,2);
    /* Full canonical project URL, wrapped at a slash. */
    identity_line(148,"https://github.com/",CYAN,2);
    identity_line(166,"mr258876/C3M1_OpenFirmware",CYAN,2);
    c3_lcd_rect(0,214,320,26,PANEL);
    c3_ui_hint(10,219,C3_HINT_SHOT,"BACK",WHITE);
}

void c3_menu_draw_mode(const c3_exposure_t *exposure)
{
    if (!exposure) return;
    title("FUNCTION MENU");
    c3_menu_draw_battery();
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
    c3_ui_hint(14,173,C3_HINT_DIAL,"ADJUST EV",WHITE);
    c3_ui_hint(160,173,C3_HINT_4X,"SETTINGS",WHITE);
    c3_ui_hint(14,193,C3_HINT_ISO,"PLAY",WHITE);
    c3_ui_hint(160,193,C3_HINT_SHOT,"BACK",WHITE);
    c3_ui_text(14,217,"MAY NEED TO RESET SHUTTER SPEED",MUTED,1);
}

void c3_menu_draw_settings(const c3_ui_nav_t *nav,
                           const c3_settings_t *settings)
{
    if (!nav || !settings) return;
    c3_i18n_set_language(c3_settings_language(settings));
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
        {
            c3_ui_text(240,y+8,c3_settings_value(settings,item),
                        selected?CYAN:WHITE,1);
        }
    }
    c3_lcd_rect(0,214,320,26,PANEL);
    c3_ui_hint(10,219,C3_HINT_DIAL,"MOVE",WHITE);
    c3_ui_hint(111,219,C3_HINT_4X,(nav->selected==C3_SETTING_ABOUT
               || nav->selected==C3_SETTING_LANGUAGE
               )?"OPEN":"SET",WHITE);
    c3_ui_hint(222,219,C3_HINT_SHOT,"EXIT",WHITE);
}

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


/* The highlighted language is used only for rendering this picker.
 * Restore the application language before returning; only confirmation
 * and successful Flash commit change the active language. */
void c3_menu_draw_language(uint8_t choice, uint8_t required, uint8_t save_error)
{
    uint8_t old=c3_i18n_is_chinese();
    c3_i18n_set_language(0u);
    title("LANGUAGE");
    c3_i18n_set_language(choice);
    /* English anchor stays fixed; avoid repeating an identical label. */
    if(choice!=0u)
        c3_ui_text(132,8,"LANGUAGE",WHITE,2);
    for (uint8_t i=0;i<2u;i++) {
        int y=64+46*i;
        uint8_t selected=(uint8_t)(choice==i);
        c3_lcd_rect(18,y,284,38,selected?ACTIVE:PANEL);
        if(selected)c3_lcd_rect(18,y,3,38,CYAN);
        c3_i18n_set_language(i);
        const char *label=i?"SIMPLIFIED CHINESE":"ENGLISH";
        c3_ui_text(36,y+12,label,selected?WHITE:MUTED,2);
    }
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
    c3_i18n_set_language(old);
}

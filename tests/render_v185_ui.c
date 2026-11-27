/* Host preview: real firmware OSD glyph mask and menu draw calls.
 * Mock LCD GRAM, not an actual camera screenshot. Outputs six RGB565 frames. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "c3_osd.h"
#include "c3_exposure.h"
#include "c3_font.h"
#include "c3_lcd.h"
#include "c3_menu.h"
#include "c3_battery.h"
static uint16_t gram[240][320];
void c3_lcd_rect(int x,int y,int w,int h,uint16_t c) {
    assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=320 && y+h<=240);
    for(int yy=y; yy<y+h;yy++) for(int xx=x;xx<x+w;xx++) gram[yy][xx]=c;
}
void c3_lcd_fill(uint16_t c){ c3_lcd_rect(0,0,320,240,c); }
void c3_lcd_text(int x,int y,const char *s,uint16_t c,int scale) {
    for(;*s;s++,x+=6*scale) {
        const uint8_t *f=c3_font_glyph(*s);
        if(f) for(int col=0;col<5;col++) for(int row=0;row<7;row++)
            if(f[col]&(1u<<row)) c3_lcd_rect(x+col*scale,y+row*scale,scale,scale,c);
    }
}
uint8_t c3_battery_read_mv(uint16_t *mv){*mv=3850;return 1;}
static void write_frame(void) {assert(fwrite(gram,sizeof(gram),1,stdout)==1);}
static void render_osd(c3_exposure_t *e,c3_meter_t *meter,uint8_t sd,uint32_t t) {
    uint8_t red=c3_osd_update_card_warning(sd,t);
    c3_osd_set_battery(75u,1u);
    uint8_t d=c3_exposure_effective_dial(e),g=c3_exposure_effective_gain(e);
    c3_osd_prepare(123,sd,1,0,1,1,72,1,294,e->shoot_mode,d,g,e->dial_code,e->ev_half_steps,c3_exposure_ev_active(e),0,0,meter);
    c3_lcd_fill(0x39E7);
    for(unsigned y=0;y<240;y++) if(y<24||y>=216) {
        const uint8_t *row=c3_osd_row(y);
        for(unsigned x=0;x<320;x++) {
            gram[y][x]=0x2104;
            if(row[x/8]&(0x80u>>(x%8))) gram[y][x]=c3_osd_glyph_color_auto(red,d==C3_EXPOSURE_AUTO_CODE,g==0,x,y);
        }
    }
    write_frame();
}
int main(void) {
    c3_exposure_t e; c3_exposure_init(&e,9,0);
    e.ev_half_steps=2;
    c3_meter_t m={.exposure_rows=80,.gain_x16=40,.valid=1};
    render_osd(&e,&m,1,0);
    c3_exposure_sync_dial(&e,4,10);m.exposure_rows=c3_exposure_rows(4);m.gain_x16=48;
    render_osd(&e,&m,1,10);
    for(int i=0;i<3;i++) c3_exposure_gain_next(&e);
    render_osd(&e,&m,1,20);
    c3_exposure_sync_dial(&e,9,30);
    render_osd(&e,&m,0,30);
    /* Test menu drawing bounds for all compensation values and sleep labels. */
    for(int v=-6;v<=6;v++){ e.ev_half_steps=v; c3_menu_draw_mode(&e); }
    e.ev_half_steps=2;c3_menu_draw_mode(&e);write_frame();
    c3_ui_nav_t n; c3_ui_nav_reset(&n,9,0);n.selected=C3_SETTING_SLEEP_TIMER;n.first_visible=6;
    c3_settings_t s;c3_settings_defaults(&s);
    for(unsigned v=0;v<=4;v++){s.sleep_timer=v;c3_menu_draw_settings(&n,&s);}
    s.sleep_timer=1;c3_menu_draw_settings(&n,&s);write_frame();
    return 0;
}

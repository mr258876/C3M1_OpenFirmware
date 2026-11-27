#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_settings.h"
#include "c3_focus.h"
#include "c3_ui_nav.h"
#include "c3_board_pins.h"
#include "c3_preview_dma_render.h"
static const char *labels[]={"FOCUS PEAK","PEAK COLOR","SENSITIVITY","FOCUS SCORE","FPS DISPLAY","GRID LINES","MONO PREVIEW","OSD DISPLAY","SAVE FORMAT","LCD ROTATE 180","BATTERY","AUTO SLEEP"};
int main(void){
 c3_settings_t s;c3_settings_defaults(&s);
 assert(C3_PEAK_FULL_SP_ONLY && sizeof(s)==12 && sizeof(c3_settings_record_t)==32 && C3_SETTINGS_VERSION==6u);
 assert(!s.peaking && c3_settings_peak_area(&s)==C3_PEAK_AREA_FULL_SPARSE);
 for(unsigned old=0;old<4;old++){
   s.peak_area=old;c3_settings_record_t r;c3_settings_make_record(&r,&s,19);
   assert(c3_settings_record_valid(&r)&&r.data.peak_area==old);
   assert(c3_settings_peak_area(&r.data)==C3_PEAK_AREA_FULL_SPARSE);
   c3_settings_t p=r.data;c3_settings_next(&p,C3_SETTING_PEAK);assert(p.peaking&&!memcmp((uint8_t*)&p+1,(uint8_t*)&r.data+1,sizeof(p)-1));
   c3_settings_next(&p,C3_SETTING_PEAK);assert(!memcmp(&p,&r.data,sizeof(p)));
   for(unsigned y=0;y<240;y++)assert(c3_peak_exp_row_active(old,y,0)==c3_peak_exp_row_active(C3_PEAK_AREA_FULL_SPARSE,y,0));
 }
 assert(C3_SETTINGS_COUNT==12);
 for(unsigned i=0;i<C3_SETTINGS_COUNT;i++)assert(!strcmp(c3_settings_label(i),labels[i]));
 c3_ui_nav_t nav;c3_ui_nav_reset(&nav,0,0);
 c3_button_events_t key={0};key.long_pressed=C3_BTN_FILM;
 assert(c3_ui_nav_poll(&nav,key,C3_BTN_FILM|C3_BTN_4X,0,700,&s)==C3_NAV_MENU_OPEN);
 assert(nav.selected==0 && nav.page==C3_UI_MENU);
 key=(c3_button_events_t){0};
 for(unsigned i=1;i<=24;i++){
  c3_ui_nav_poll(&nav,key,0,i&15,700+i*100,&s);
  assert(c3_ui_nav_poll(&nav,key,0,i&15,760+i*100,&s)&C3_NAV_CURSOR);
  assert(nav.selected==i%12 && nav.first_visible<=nav.selected && nav.selected<nav.first_visible+7);
 }
 for(unsigned i=1;i<=24;i++){
  c3_ui_nav_poll(&nav,key,0,(24-i)&15,4000+i*100,&s);
  assert(c3_ui_nav_poll(&nav,key,0,(24-i)&15,4060+i*100,&s)&C3_NAV_CURSOR);
  assert(nav.selected==(24-i)%12);
 }
 for(unsigned i=0;i<5;i++){
    assert(s.focus_score==i);c3_settings_next(&s,C3_SETTING_SCORE);
 }
 assert(s.focus_score==0);
 /* Score-only renderer: unchanged sums, sampling cadence, smoothing for
    every independent mode, before overlays; compare independent score API. */
 for(unsigned m=0;m<5;m++){
  c3_score_t a,b;c3_score_reset(&a);c3_score_reset(&b);
  c3_focus_t f;c3_focus_reset(&f);c3_peak_exp_t exp={0};
  for(unsigned k=0;k<4;k++){
   unsigned sample=c3_score_schedule_frame(&a,m,0);assert(sample==c3_score_schedule_frame(&b,m,0));
   c3_live_render_t p={.grid=1,.mono=k&1,.peaking=0,.score_center=c3_score_is_center(m),.score_sample=sample,.score_mode=m,.peak_area=c3_settings_peak_area(&s)};
   for(unsigned y=0;y<240;y++){
    uint8_t row[640];for(unsigned x=0;x<320;x++)c3_effect_put565(row,x,(uint16_t)(x*31+y*593+k*311));
    if(sample&&c3_score_row_active(m,y))c3_score_sample_row(&b,y,row);
    c3_live_render_row(row,y,0,&p,&f,&a,&exp);
   }
   if(sample){c3_score_end_frame(&a);c3_score_end_frame(&b);}
   assert(a.sum==b.sum&&a.count==b.count&&a.score==b.score);
  }
 }
 puts("PASS v202: 4 legacy v6 areas accepted and ignored, menu 12 items, 48 navigation steps, independent FS modes unchanged");
}

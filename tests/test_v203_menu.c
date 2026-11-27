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
 for(unsigned old=0;old<5;old++){
   s.focus_score=old;
   c3_settings_record_t r;c3_settings_make_record(&r,&s,19);
   assert(c3_settings_record_valid(&r)&&r.data.focus_score==old);
   assert(c3_score_effective_mode(old)==(old?C3_SCORE_FULL_SPARSE:C3_SCORE_OFF));
   assert(!strcmp(c3_settings_value(&s,C3_SETTING_SCORE),old?"ON":"OFF"));
   assert(!c3_score_is_center(old));
   c3_settings_next(&s,C3_SETTING_SCORE);assert(s.focus_score==(old?0:4));
   c3_settings_next(&s,C3_SETTING_SCORE);assert(s.focus_score==(old?4:0));
   c3_score_t score;c3_score_reset(&score);
   for(unsigned frame=0;frame<4;frame++)assert(c3_score_schedule_frame(&score,old,frame&1)==(old!=0));
   assert(score.mode==(old?4:0));
 }
 s.focus_score=5;assert(!c3_settings_valid(&s));assert(c3_score_effective_mode(5)==0);
 for(unsigned y=0;y<240;y++){
   unsigned active=(y>=23&&y<=212&&(y%4==0||y%4==3));
   assert(c3_score_row_active(C3_SCORE_FULL_SPARSE,y)==active);
 }
 puts("PASS v203: 4 old peak areas, 5 old score values, OFF/ON cycle, 12 menu items, 48 navigation steps, per-frame cadence and 96 active score rows");
}

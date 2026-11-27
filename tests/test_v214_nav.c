#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_ui_nav.h"
#include "c3_board_pins.h"
static uint16_t poll(c3_ui_nav_t *n,c3_settings_t *s,uint16_t pressed,uint16_t stable,uint8_t dial){
 static uint32_t now=100; c3_button_events_t k={0};k.pressed=pressed; now+=100;
 return c3_ui_nav_poll(n,k,stable,dial,now,s);
}
int main(void){
 c3_settings_t s; c3_settings_defaults(&s);s.peak_area=4;s.peaking=1;
 const c3_settings_t before=s; c3_ui_nav_t n;
 assert(C3_SETTING_ABOUT==C3_SETTINGS_COUNT-1);
 for(int i=0;i<C3_SETTINGS_COUNT;i++)assert(strcmp(c3_settings_label(i),"BATTERY"));
 for(unsigned d=0;d<16;d++){
  c3_ui_nav_reset(&n,d,0);n.page=C3_UI_MENU;n.selected=C3_SETTING_ABOUT;n.first_visible=C3_SETTINGS_COUNT-7;
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,d)==C3_NAV_ABOUT_OPEN);
  assert(n.page==C3_UI_ABOUT);
  assert(!poll(&n,&s,0,C3_BTN_4X,d)); // held key
  assert(!poll(&n,&s,C3_BTN_ISO,C3_BTN_ISO,(d+1)&15));
  assert(!poll(&n,&s,C3_BTN_4X,C3_BTN_4X,(d+5)&15));
  assert(!poll(&n,&s,0,0,(d+5)&15));
  assert(poll(&n,&s,C3_BTN_SHUTTER|C3_BTN_4X,C3_BTN_SHUTTER|C3_BTN_4X,(d+5)&15)==C3_NAV_ABOUT_BACK);
  assert(n.page==C3_UI_MENU&&n.selected==C3_SETTING_ABOUT&&n.first_visible==C3_SETTINGS_COUNT-7);
  assert(!poll(&n,&s,0,C3_BTN_SHUTTER,(d+5)&15)); // no repeat
  assert(!poll(&n,&s,0,0,(d+5)&15));
  assert(poll(&n,&s,C3_BTN_SHUTTER,C3_BTN_SHUTTER,(d+5)&15)==C3_NAV_BACK);
  assert(n.page==C3_UI_PREVIEW&&!memcmp(&s,&before,sizeof s));
 }
 puts("PASS v214 about navigation: 16 dial positions, no settings mutation, shutter returns to settings, held key / chord / rotary safety");
 return 0;
}

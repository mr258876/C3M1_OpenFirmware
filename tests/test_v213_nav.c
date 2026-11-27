#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_ui_nav.h"
#include "c3_board_pins.h"
static uint16_t poll(c3_ui_nav_t *n,c3_settings_t *s,uint16_t pressed,uint16_t stable,uint8_t dial){
 static uint32_t now; c3_button_events_t k={0};k.pressed=pressed; now+=100;
 return c3_ui_nav_poll(n,k,stable,dial,now,s);
}
int main(void){
 unsigned cases=0;
 for(unsigned item=C3_SETTING_RESET;item<=C3_SETTING_FORMAT;item++){
  c3_ui_nav_t n;c3_settings_t s; c3_settings_defaults(&s);s.peaking=1;s.peak_area=4;
  const c3_settings_t before=s;
  c3_ui_nav_reset(&n,0,0);n.page=C3_UI_MENU;n.selected=item;
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,0)==C3_NAV_CONFIRM_OPEN);
  assert(n.page==C3_UI_CONFIRM&&!n.confirm_armed);
  assert(!poll(&n,&s,C3_BTN_ISO,C3_BTN_ISO|C3_BTN_4X,1)); // chord
  assert(!poll(&n,&s,C3_BTN_ISO,C3_BTN_ISO,1)); // ISO before release
  assert(!poll(&n,&s,0,0,1)&&n.confirm_armed);
  assert(!poll(&n,&s,C3_BTN_4X,C3_BTN_4X,2)); // repeat 4X
  assert(!poll(&n,&s,C3_BTN_ISO,C3_BTN_ISO,2)); // no release
  assert(!poll(&n,&s,0,0,4));
  assert(poll(&n,&s,C3_BTN_ISO|C3_BTN_SHUTTER,C3_BTN_ISO|C3_BTN_SHUTTER,4)==C3_NAV_CONFIRM_CANCEL);
  assert(n.page==C3_UI_MENU&&!memcmp(&s,&before,sizeof s));
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,4)==C3_NAV_CONFIRM_OPEN);
  assert(!poll(&n,&s,0,0,5));
  assert(poll(&n,&s,C3_BTN_ISO,C3_BTN_ISO,6)==(item==C3_SETTING_RESET?C3_NAV_RESET:C3_NAV_FORMAT));
  assert(n.page==C3_UI_MENU&&!memcmp(&s,&before,sizeof s));
  assert(!poll(&n,&s,0,C3_BTN_ISO,6)); // no repeat
  cases++;
 }
 puts("PASS confirmation gating (reset & format): fresh ISO, release, chords, cancel priority, dial ignored, no repeated action or premature mutation");
 return cases!=2;
}

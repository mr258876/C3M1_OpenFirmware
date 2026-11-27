#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_ui_nav.h"
#include "c3_board_pins.h"
static const unsigned buttons[]={C3_BTN_ISO,C3_BTN_SHUTTER,C3_BTN_4X,C3_BTN_FILM};
static uint16_t mask(unsigned i){uint16_t m=0;for(unsigned j=0;j<4;j++)if(i&(1u<<j))m|=buttons[j];return m;}
static uint16_t poll(c3_ui_nav_t *n,c3_settings_t *s,uint16_t press,uint16_t stable,uint8_t dial,uint32_t now){
 c3_button_events_t k={0};k.pressed=press;return c3_ui_nav_poll(n,k,stable,dial,now,s);
}
int main(void){
 unsigned count=0; c3_settings_t s; c3_settings_defaults(&s);
 for(unsigned required=0;required<2;required++)for(unsigned choice=0;choice<2;choice++)for(unsigned d=0;d<16;d++){
  c3_ui_nav_t n; c3_ui_nav_reset(&n,d,0); n.page=C3_UI_MENU;n.selected=C3_SETTING_LANGUAGE;
  const c3_settings_t before=s;
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,d,100)==C3_NAV_LANGUAGE_OPEN);
  assert(n.page==C3_UI_MENU&&n.selected==C3_SETTING_LANGUAGE&&!memcmp(&s,&before,sizeof s));
  for(unsigned armed=0;armed<2;armed++)for(unsigned p=0;p<16;p++)for(unsigned stable=0;stable<16;stable++){
   c3_ui_nav_language(&n,d,UINT32_MAX-100,choice,required);
   if(armed)assert(!poll(&n,&s,0,0,d,UINT32_MAX-50));
   uint16_t a=poll(&n,&s,mask(p),mask(stable),d,5);
   uint16_t expected=(!required&&(mask(p)&C3_BTN_SHUTTER))?C3_NAV_CONFIRM_CANCEL:
    (armed&&mask(p)==C3_BTN_4X&&mask(stable)==C3_BTN_4X)?C3_NAV_LANGUAGE_CONFIRM:0;
   assert(a==expected&&n.page==C3_UI_LANGUAGE&&!memcmp(&s,&before,sizeof s));count++;
  }
  c3_ui_nav_language(&n,d,UINT32_MAX-100,choice,required);
  assert(!poll(&n,&s,0,0,(d+1)&15,UINT32_MAX-40));
  assert(!poll(&n,&s,0,0,(d+1)&15,UINT32_MAX-1));
  assert(poll(&n,&s,0,0,(d+1)&15,25)==C3_NAV_CURSOR&&n.language_choice!=choice);
  assert(!poll(&n,&s,0,0,d,50));
  assert(poll(&n,&s,0,0,d,120)==C3_NAV_CURSOR&&n.language_choice==choice);
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,d,140)==C3_NAV_LANGUAGE_CONFIRM);
  assert(!poll(&n,&s,C3_BTN_4X,C3_BTN_4X,d,150)); // no repeat after a failed save
  assert(!poll(&n,&s,0,0,d,160));
  assert(poll(&n,&s,C3_BTN_4X,C3_BTN_4X,d,170)==C3_NAV_LANGUAGE_CONFIRM);
 }
 printf("PASS language picker: %u key/hold/chord cases, dial debounce and wrap, no settings mutation or forced cancel, retry gating\n",count);
}

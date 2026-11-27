#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "c3_i18n.h"
#include "c3_startup_hint.h"
#include "c3_ui_hints.h"
#include "c3_font.h"
#include "c3_menu.h"
#include "c3_board_pins.h"
#include "c3_battery.h"
#include "c3_i18n_data.inc"
static uint16_t fb[240][320];
static unsigned rects,ascii_calls;
static unsigned win_x,win_y,win_w,win_h,win_pos,win_calls;
void c3_lcd_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){
 assert(x0<=x1&&y0<=y1&&x1<320&&y1<240);
 win_x=x0;win_y=y0;win_w=x1-x0+1;win_h=y1-y0+1;win_pos=0;win_calls++;
}
void c3_lcd_pixel_stream(uint16_t color){assert(win_pos<win_w*win_h);fb[win_y+win_pos/win_w][win_x+win_pos%win_w]=color;win_pos++;}

void c3_lcd_rect(int x,int y,int w,int h,uint16_t color){
 assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);rects++;
 for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)fb[j][i]=color;
}
void c3_lcd_fill(uint16_t color){c3_lcd_rect(0,0,320,240,color);}
void c3_lcd_text(int x,int y,const char *s,uint16_t color,int scale){
 ascii_calls++;for(;*s;s++,x+=6*scale){assert((unsigned char)*s<128);const uint8_t *f=c3_font_glyph(*s);if(!f)continue;
 for(int row=0;row<7;row++)for(int c=0;c<5;c++)if((f[c]>>row)&1)c3_lcd_rect(x+c*scale,y+row*scale,scale,scale,color);}
}
void c3_lcd_message(const char *s){c3_lcd_rect(22,97,276,38,0);c3_lcd_text(30,108,s,0xffff,2);}
static unsigned adc_reads;
static uint16_t mock_mv=3850;
static uint8_t mock_adc_ok=1;
uint8_t c3_battery_read_mv(uint16_t *mv){adc_reads++;*mv=mock_mv;return mock_adc_ok;}
void c3_battery_format(char out[16],uint16_t mv){
 if(mv<2500)strcpy(out,"BAT LOW");else if(mv>=4200)strcpy(out,"4.20V ~100%");else strcpy(out,"3.85V ~58%");
}
void c3_battery_format_unknown(char out[16]){strcpy(out,"ADC ERR");}
/* Only menu rendering stubs; exposure state machine is tested separately. */
uint8_t c3_exposure_ev_active(const c3_exposure_t *s){return s->shoot_mode!=C3_SHOOT_M;}
void c3_exposure_ev_label(char out[7],int8_t ev){strcpy(out,"EV+0.0");out[2]=ev<0?'-':'+';unsigned n=ev<0?-ev:ev;out[3]='0'+n/2;out[5]='0'+(n%2)*5;}
static void ppm(const char *dir,const char *name){char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");for(int y=0;y<240;y++)for(int x=0;x<320;x++){uint16_t v=fb[y][x];unsigned char rgb[3]={(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}fclose(f);}
static void check_settings(void){
 c3_settings_t s;c3_settings_defaults(&s);assert(C3_SETTINGS_COUNT==(C3_POSTSHOT?18:16)&&sizeof(s)==12&&sizeof(c3_settings_record_t)==32&&C3_SETTINGS_VERSION==6);
#if C3_LANGUAGE_SETUP
 assert(c3_settings_language_pending(&s));
 for(unsigned code=0;code<=7;code++){
  s.peak_area=code; s.peaking=1; s.focus_score=4; s.sleep_timer=2;
  c3_settings_t old=s;
  assert(c3_settings_valid(&s)&&c3_settings_language_pending(&s)==(code==7));
  assert(c3_settings_language(&s)==(code==4||code==6));
  assert(c3_settings_startup_hint(&s)==(code<=4||code==7));
  c3_settings_record_t r; c3_settings_make_record(&r,&s,42);
  assert(c3_settings_record_valid(&r)&&!memcmp(&s,&r.data,sizeof s));
  c3_settings_next(&s,C3_SETTING_LANGUAGE);assert(!memcmp(&s,&old,sizeof s));
  if(code!=7){c3_settings_next(&s,C3_SETTING_STARTUP_HINT);assert(c3_settings_language(&s)==c3_settings_language(&old)&&c3_settings_startup_hint(&s)!=c3_settings_startup_hint(&old));}
 }
 s.peak_area=C3_POSTSHOT?32:8;assert(!c3_settings_valid(&s));
 puts("PASS v216 settings v6 layout; all confirmed codes and pending state; hint independent");
#else
 for(unsigned a=0;a<4;a++){
  s.peak_area=a;s.peaking=1;s.focus_score=4;s.sleep_timer=2;c3_settings_t old=s;
  c3_settings_record_t r;c3_settings_make_record(&r,&s,99);assert(c3_settings_record_valid(&r)&&!c3_settings_language(&r.data));
  c3_settings_next(&s,C3_SETTING_LANGUAGE);assert(c3_settings_language(&s)&&c3_settings_peak_area(&s)==3);
  assert(c3_settings_valid(&s)&&!memcmp(&s,&old,10)&&s.sleep_timer==old.sleep_timer);
  c3_settings_make_record(&r,&s,100);assert(c3_settings_record_valid(&r)&&c3_settings_language(&r.data));
  c3_settings_next(&s,C3_SETTING_LANGUAGE);assert(!c3_settings_language(&s)&&s.peak_area==3);
 }
 s.peak_area=7;assert(!c3_settings_valid(&s));c3_settings_defaults(&s);
 for(unsigned code=0;code<=6;code++){
  s.peak_area=code; c3_settings_t old=s;
  uint8_t zh=c3_settings_language(&s),hint=c3_settings_startup_hint(&s);
  assert(zh==(code==4||code==6)&&hint==(code<=4));
  c3_settings_next(&s,C3_SETTING_STARTUP_HINT);assert(c3_settings_language(&s)==zh&&c3_settings_startup_hint(&s)!=hint);
  c3_settings_record_t r;c3_settings_make_record(&r,&s,42);assert(c3_settings_record_valid(&r)&&!memcmp(&s,&r.data,sizeof s));
  assert(!memcmp(&s,&old,10)&&s.sleep_timer==old.sleep_timer);
  c3_settings_next(&s,C3_SETTING_LANGUAGE);assert(c3_settings_language(&s)!=zh&&c3_settings_startup_hint(&s)!=hint);
  c3_settings_next(&s,C3_SETTING_STARTUP_HINT);assert(c3_settings_language(&s)!=zh&&c3_settings_startup_hint(&s)==hint);
 }
 c3_settings_defaults(&s);assert(c3_settings_startup_hint(&s));
 c3_ui_nav_t n;c3_ui_nav_reset(&n,0,0);n.page=C3_UI_MENU;
 c3_button_events_t key={0};
 c3_ui_nav_poll(&n,key,0,15,100,&s);assert(c3_ui_nav_poll(&n,key,0,15,200,&s)&C3_NAV_CURSOR);
 #if C3_FUNCTION_MENU
 assert(n.selected==C3_SETTING_ABOUT && n.first_visible==C3_SETTINGS_COUNT-7);
 n.selected=C3_SETTING_LANGUAGE;
 #elif C3_MENU_ACTIONS
 assert(n.selected==C3_SETTING_FORMAT && n.first_visible==8);
 n.selected=C3_SETTING_LANGUAGE;
#else
 assert(n.selected==C3_SETTING_LANGUAGE&&n.first_visible==6);
#endif
 key.pressed=C3_BTN_4X;assert(c3_ui_nav_poll(&n,key,C3_BTN_4X,15,300,&s)&C3_NAV_SETTING);assert(c3_settings_language(&s));
 assert(c3_ui_nav_poll(&n,key,C3_BTN_4X,15,400,&s)&C3_NAV_SETTING);assert(!c3_settings_language(&s));
 puts("PASS v210 v6 record compatibility, language setting and rotary wrap");
#endif
}
#if C3_UI_ICONS
static void check_hint_cells(void){
 /* Reserve the same cells used in the actual screen layouts. Verify every
  * foreground pixel remains within its own cell in both languages. */
 static const struct {int x,y,w; c3_hint_key_t key; const char *action;} cases[]={
  {14,173,146,C3_HINT_DIAL,"ADJUST EV"},{160,173,146,C3_HINT_4X,"SETTINGS"},
  {14,193,146,C3_HINT_ISO,"PLAY"},{160,193,146,C3_HINT_SHOT,"BACK"},
  {10,219,101,C3_HINT_DIAL,"MOVE"},{111,219,111,C3_HINT_4X,"SET"},
  {111,219,111,C3_HINT_4X,"REFRESH"},{222,219,98,C3_HINT_SHOT,"EXIT"},
  {6,224,116,C3_HINT_DIAL,"PREV/NEXT"},{122,224,100,C3_HINT_ISO,"INFO"},
  {222,224,98,C3_HINT_SHOT,"BACK"},{22,144,276,C3_HINT_SHOT,"RETRY"},
  {22,162,276,C3_HINT_FILM,"HOLD - DISCARD"}
 };
 for(unsigned lang=0;lang<2;lang++){
  c3_i18n_set_language(lang);
  for(unsigned n=0;n<sizeof cases/sizeof cases[0];n++){
   c3_lcd_fill(0);c3_ui_hint(cases[n].x,cases[n].y,cases[n].key,cases[n].action,0xffff);
   unsigned drawn=0;for(int y=0;y<240;y++)for(int x=0;x<320;x++)if(fb[y][x]){
    assert(x>=cases[n].x&&x<cases[n].x+cases[n].w&&y>=cases[n].y&&y<cases[n].y+16);drawn++;
   }assert(drawn>20);
  }
 }
 puts("PASS v212 26 bilingual hint cell tests (EV/settings/battery/playback/recovery), no overlap or clipping");
}
#endif
static void check_boot_hint(const char *dir){
 static uint16_t stream[240][320];
 for(unsigned lang=0;lang<2;lang++){
  c3_i18n_set_language(lang);c3_lcd_fill(0x39E7u);unsigned calls=win_calls;
  c3_ui_draw_startup_hint();assert(win_calls==calls+1&&win_pos==win_w*win_h&&win_y==194&&win_h==20);
  int x=(int)win_x,w=(int)win_w;
  memcpy(stream,fb,sizeof fb);ppm(dir,lang?"zh_boot_hint":"en_boot_hint");
  for(int y=0;y<240;y++)for(int i=0;i<320;i++)if(y<194||y>213||i<x||i>=x+w)assert(fb[y][i]==0x39E7u);
  c3_lcd_rect(x,194,w,20,0);c3_ui_hint(x+8,196,C3_HINT_FILM,C3_STARTUP_HINT_KEY,0xffffu);
  assert(!memcmp(stream,fb,sizeof fb));
 }
 // First successful frame; unsigned wrap; disabled; early dismiss; no re-arm on resume.
 for(uint32_t start=0;start<1000;start++){
  uint32_t t=UINT32_MAX-500u+start; c3_startup_hint_t s={0};
  assert(!c3_startup_hint_frame(&s,1,0,t-2000u)&&s.state==0);
  assert(c3_startup_hint_frame(&s,1,1,t));assert(!c3_startup_hint_frame(&s,1,0,t+100));
  assert(c3_startup_hint_frame(&s,1,1,t+4999u));assert(!c3_startup_hint_frame(&s,1,1,t+5000u));
  assert(!c3_startup_hint_frame(&s,1,1,t+9000u));
  s=(c3_startup_hint_t){0};assert(!c3_startup_hint_frame(&s,0,1,t));assert(!c3_startup_hint_frame(&s,1,1,t+1));
  s=(c3_startup_hint_t){0};assert(c3_startup_hint_frame(&s,1,1,t));c3_startup_hint_dismiss(&s);assert(!c3_startup_hint_frame(&s,1,1,t+1));
 }
 puts("PASS v215 startup hint: exact pixels vs static hint renderer, single LCD window, 1000 wrap-safe timer cases, independent setting and dismissal");
}
int main(int argc,char **argv){
 assert(argc==2);check_boot_hint(argv[1]);
#if C3_LANGUAGE_SETUP
 static uint16_t picker[240][320];
 for(unsigned required=0;required<2;required++)for(unsigned choice=0;choice<2;choice++)for(unsigned error=0;error<2;error++){
  c3_i18n_set_language(0);c3_menu_draw_language(choice,required,error);assert(!c3_i18n_is_chinese());
  memcpy(picker,fb,sizeof fb);c3_i18n_set_language(1);c3_menu_draw_language(choice,required,error);
  assert(c3_i18n_is_chinese()&&!memcmp(picker,fb,sizeof fb));
  // Check both highlighting and cancellation cell. Confirm/move icons remain visible.
  assert(fb[choice?110:64][18]==0x07FFu);
  for(int y=208;y<240;y++)for(int x=222;x<320;x++)if(required)assert(fb[y][x]==0x1082u);
  char name[64];snprintf(name,sizeof name,"language_%u_%u_%u",required,choice,error);ppm(argv[1],name);
#if C3_LANGUAGE_LAYOUT
  // Verify the title, single localized message and three hint cells
  // independently against the fixed English anchor + selected locale.
  memcpy(picker,fb,sizeof fb);
  c3_lcd_rect(0,0,320,31,0x1082u);
  c3_i18n_set_language(0);c3_ui_text(12,8,"LANGUAGE",0xffff,2);
  c3_i18n_set_language(choice);
  if(choice)c3_ui_text(132,8,"LANGUAGE",0xffff,2);
  assert(!memcmp(picker,fb,31*320*sizeof(uint16_t)));
  c3_lcd_rect(0,148,320,60,0x0841u);
  if(error||required)c3_ui_text(18,168,error?"SET SAVE ERROR":"SELECT LANGUAGE TO CONTINUE",error?0xf800u:0xa534u,1);
  assert(!memcmp(picker[148],fb[148],60*320*sizeof(uint16_t)));
  c3_lcd_rect(0,208,320,32,0x1082u);
  c3_i18n_set_language(0);c3_ui_hint(8,216,C3_HINT_DIAL,"MOVE",0xffffu);
  c3_i18n_set_language(choice);
  if(choice)c3_ui_text(58,220,"MOVE",0xffffu,1);
  c3_ui_hint(110,216,C3_HINT_4X,"CONFIRM",0x07ffu);
  if(!required)c3_ui_hint(222,216,C3_HINT_SHOT,"CANCEL",0xffffu);
  assert(!memcmp(picker[208],fb[208],32*320*sizeof(uint16_t)));
  // Each footer cell must fit even in the longer supported locale.
  assert(58+c3_i18n_width(c3_i18n_translate("MOVE"),1)<=110);
  assert(138+c3_i18n_width(c3_i18n_translate("CONFIRM"),1)<=222);
  assert(244+c3_i18n_width(c3_i18n_translate("CANCEL"),1)<=320);
  c3_i18n_set_language(1);
#endif
 }
#if C3_LANGUAGE_LAYOUT
 puts("PASS v217 16 picker renders: fixed English anchors, selected-language title/hints/message, preserved active language, forced page no cancel, bounds");
#else
 puts("PASS v216 16 bilingual picker renders, language-independent pixels, forced page no cancel, highlight and bounds");
#endif
#endif
#if C3_UI_ICONS
 check_hint_cells();
#endif
 assert(argc==2);check_settings();c3_settings_t s;c3_settings_defaults(&s);s.peaking=1;s.focus_score=4;s.fps=1;s.sleep_timer=2;
 unsigned menus=0,strings=0;
 for(unsigned lang=0;lang<2;lang++){
  s.peak_area=lang?4:3;c3_i18n_set_language(lang);
  for(unsigned i=0;i<sizeof(translations)/sizeof(translations[0]);i++){
   const char *v=c3_i18n_translate(translations[i].en);assert(!strcmp(v,lang?translations[i].zh:translations[i].en));
   if(lang){int w=c3_i18n_width(v,1);assert(w<=300);unsigned before=rects;c3_ui_text(0,16,translations[i].en,0xffff,1);assert(rects>before);strings++;}
  }
#if C3_FUNCTION_MENU
  const unsigned before_menu_reads=adc_reads;
#endif
  for(unsigned sel=0;sel<C3_SETTINGS_COUNT;sel++){
   c3_ui_nav_t n={0};n.selected=sel;n.first_visible=sel>6?sel-6:0;n.page=C3_UI_MENU;
   for(unsigned color=0;color<4;color++)for(unsigned sleep=0;sleep<5;sleep++)for(unsigned level=0;level<3;level++){
    s.color=color;s.sleep_timer=sleep;s.sensitivity=level;
    for(unsigned i=0;i<C3_SETTINGS_COUNT;i++){
     const char *v=c3_i18n_translate(c3_settings_value(&s,i));
#if !C3_FUNCTION_MENU
     if(i!=C3_SETTING_BATTERY)
#endif
     assert(c3_i18n_width(v,1)<=70);
     assert(c3_i18n_width(c3_i18n_translate(c3_settings_label(i)),1)<=205);
    }
    c3_menu_draw_settings(&n,&s);assert(c3_i18n_is_chinese()==lang);menus++;
   }
   #if C3_MENU_ACTIONS
   if(sel==C3_SETTING_FORMAT)ppm(argv[1],lang?"zh_actions":"en_actions");
#endif
#if C3_FUNCTION_MENU
   if(sel==C3_SETTING_ABOUT)ppm(argv[1],lang?"zh_about_select":"en_about_select");
   if(sel==C3_SETTING_STARTUP_HINT)ppm(argv[1],lang?"zh_hint_setting":"en_hint_setting");
#endif
   if(sel==0)ppm(argv[1],lang?"zh_settings":"en_settings");
   if(sel==C3_SETTING_LANGUAGE){s.sleep_timer=2;c3_menu_draw_settings(&n,&s);ppm(argv[1],lang?"zh_language":"en_language");}
  }
#if C3_FUNCTION_MENU
  assert(adc_reads==before_menu_reads);
  assert(12+c3_i18n_width(c3_i18n_translate("FUNCTION MENU"),2)<=174);
  assert(308-c3_i18n_width("4.20V ~100%",2)>=174);
#endif
  c3_exposure_t e={0};e.shoot_mode=C3_SHOOT_S;for(int ev=-6;ev<=6;ev++){e.ev_half_steps=ev;c3_menu_draw_mode(&e);}
  e.ev_half_steps=0;c3_menu_draw_mode(&e);ppm(argv[1],lang?"zh_ev":"en_ev");
#if C3_FUNCTION_MENU
  unsigned reads=adc_reads;
  static uint16_t old[240][320]; memcpy(old,fb,sizeof fb);
  // Only the reserved header region may change during battery refresh.
  for(unsigned j=0;j<4;j++){
   mock_mv=j==0?4200:j==1?2000:3850; mock_adc_ok=j!=2;
   c3_menu_draw_battery();
   assert(adc_reads==reads+j+1);
   for(int y=0;y<240;y++)for(int x=0;x<320;x++)if(y>=31||x<174)assert(old[y][x]==fb[y][x]);
  }
  mock_mv=3850;mock_adc_ok=1;
  reads=adc_reads;c3_menu_draw_about();assert(adc_reads==reads);
  ppm(argv[1],lang?"zh_about":"en_about");
  c3_ui_nav_t n={0};n.page=C3_UI_MENU;n.selected=C3_SETTING_ABOUT;n.first_visible=C3_SETTINGS_COUNT-7;
  c3_menu_draw_settings(&n,&s);assert(adc_reads==reads);
#endif
  e.shoot_mode=C3_SHOOT_M;c3_menu_draw_mode(&e);
#if C3_MENU_ACTIONS
  c3_menu_draw_confirm(C3_SETTING_RESET);ppm(argv[1],lang?"zh_reset":"en_reset");
  c3_menu_draw_confirm(C3_SETTING_FORMAT);ppm(argv[1],lang?"zh_format":"en_format");
  c3_menu_draw_format_status("FORMATTING",1u);ppm(argv[1],lang?"zh_formatting":"en_formatting");
  c3_menu_draw_format_status("FORMAT MOUNT ERROR",0u);
#endif
  if(lang){c3_lcd_fill(0x0841);c3_ui_message("4X LOCK FAIL");ppm(argv[1],"zh_error");}
 }
 // Decoder: truncated 1/2-byte sequences never read beyond the terminator.
 assert(c3_i18n_width("\xe4",1)==17&&c3_i18n_width("\xe4\xb8",1)==34);
 assert(c3_i18n_width("\xc2",1)==17&&c3_i18n_width("ABC",2)==36);
 printf("PASS v210 %u menu renders, %u translated strings, screen bounds and widths, mixed ASCII/UTF-8, actual dot-matrix previews\n",menus,strings);
 return 0;
}

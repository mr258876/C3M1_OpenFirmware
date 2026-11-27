/* Test the actual foreground postshot_wait implementation extracted from
 * main.c by run_v221_tests.sh, with the real debouncer and fake time/LCD/SD. */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "c3_settings.h"
#include "c3_input.h"
#include "c3_ui_nav.h"
#include "c3_postshot.h"
#include "c3_ui_hints.h"
#include "c3_i18n.h"
#include "c3_font.h"
static uint32_t tick,epoch,show_at,hint_at;
static unsigned load_count,hints,fill_count,saved_count;
static uint8_t read_ok,format_expected;
static uint32_t id_expected;
static struct event {uint32_t t;uint16_t keys;} events[16];
static unsigned count;
static uint32_t HAL_GetTick(void){return tick;}
static uint16_t read_buttons(void){uint16_t m=0;for(unsigned i=0;i<count;i++)if((uint32_t)(tick-epoch)>=events[i].t)m=events[i].keys;return m;}
static uint8_t read_shutter_dial(void){return 3;}
static void HAL_Delay(uint32_t delay){while(delay--){tick++;if((tick-epoch)%5u==0u)c3_input_sample(read_buttons(),tick);assert(tick-epoch<100000u);}}
static void c3_sd_led_poll(uint32_t t){(void)t;}
static uint32_t c3_storage_next_number(void){return id_expected+1;}
uint8_t c3_playback_show_saved(uint32_t id,c3_output_format_t fmt){assert(id==id_expected&&fmt==format_expected);load_count++;HAL_Delay(800);show_at=tick-epoch;return read_ok;}
/* Host LCD GRAM mock: retain the last preview and run real i18n/icon
 * drawing. No framebuffer is added to the firmware itself. */
static uint16_t fb[240][320];
static uint16_t background(int x,int y){return (uint16_t)(1+((x*19+y*73)%0xfffd));}
void c3_lcd_rect(int x,int y,int w,int h,uint16_t c){
 assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);
 for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)fb[j][i]=c;
}
void c3_lcd_fill(uint16_t c){fill_count++;c3_lcd_rect(0,0,320,240,c);}
void c3_lcd_text(int x,int y,const char *s,uint16_t color,int scale){
 for(;*s;s++,x+=6*scale){const uint8_t *f=c3_font_glyph(*s);if(!f)continue;
 for(int r=0;r<7;r++)for(int c=0;c<5;c++)if((f[c]>>r)&1)c3_lcd_rect(x+c*scale,y+r*scale,scale,scale,color);}
}
void c3_lcd_message(const char *s){c3_lcd_rect(22,97,276,38,0);c3_lcd_text(30,108,s,0xffff,2);}
static void test_ui_message(const char *s){assert(!strcmp(s,"SAVED"));saved_count++;c3_ui_message(s);}
static void test_ui_hint(int x,int y,c3_hint_key_t k,const char *s,uint16_t color){assert(x>=0&&y==220&&k==C3_HINT_FILM&&!strcmp(s,"WIND FILM LEVER")&&color==0xffff);hints++;hint_at=tick-epoch;c3_ui_hint(x,y,k,s,color);}
#define c3_ui_message test_ui_message
#define c3_ui_hint test_ui_hint
#include "v221_modal.inc"
#undef c3_ui_message
#undef c3_ui_hint
static void check_bg(unsigned retried){
 unsigned saved_white=0,hint_white=0;
 for(int y=0;y<240;y++)for(int x=0;x<320;x++){
  unsigned saved=x>=22&&x<298&&y>=97&&y<135;
  unsigned hint=y>=216;
  unsigned recovery=retried&&x>=12&&x<308&&y>=76&&y<189;
  if(!saved&&!hint&&!recovery)assert(fb[y][x]==background(x,y));
  if(recovery&&!saved)assert(fb[y][x]==0);
  if(saved&&fb[y][x]==0xffff)saved_white++;
  if(hint&&fb[y][x]==0xffff)hint_white++;
 }
 assert(saved_white>0&&hint_white>0&&fill_count==0);
}
static void reset(void){epoch=tick=UINT32_MAX-500u;show_at=hint_at=0;load_count=hints=fill_count=saved_count=count=0;read_ok=1;format_expected=C3_OUTPUT_DNG;id_expected=713;for(int y=0;y<240;y++)for(int x=0;x<320;x++)fb[y][x]=background(x,y);}
static void add(uint32_t t,uint16_t m){assert(count<16);events[count++]=(struct event){t,m};}
static uint32_t run(unsigned review,unsigned wind){c3_settings_t s;c3_settings_defaults(&s);if(!review)c3_settings_next(&s,C3_SETTING_REVIEW);if(!wind)c3_settings_next(&s,C3_SETTING_WIND);uint8_t sd=1;assert(postshot_wait(&s,(c3_output_format_t)format_expected,&sd)==!!(review||wind));assert(sd==(review?read_ok:1));return tick-epoch;}
static void scenarios(void){
 reset();assert(run(0,0)==0&&!load_count&&!hints);
 reset();assert(run(1,0)==5800&&load_count==1&&show_at==800&&!hints);
 reset();add(1500,C3_BTN_SHUTTER);assert(run(1,0)==1520&&!hints);
 reset();add(200,C3_BTN_ISO);add(300,0);assert(run(1,0)==800&&!hints);
 reset();add(1000,C3_BTN_FILM);assert(run(0,1)==1020&&hints==1&&hint_at==0&&!load_count&&fill_count==0&&saved_count==1);check_bg(0);
 reset();add(1000,C3_BTN_ISO);add(1100,0);add(2000,C3_BTN_SHUTTER);add(2100,0);add(6000,C3_BTN_4X);add(6100,0);add(65000,C3_BTN_FILM);assert(run(1,1)==65020&&hints==1&&hint_at==800&&load_count==1);
 reset();add(0,C3_BTN_FILM);add(200,0);add(6100,C3_BTN_FILM);assert(run(1,1)==6120&&hints==1&&hint_at==800);
 reset();add(200,C3_BTN_FILM);add(300,0);assert(run(1,1)==800&&hints==1&&hint_at==800&&load_count==1);
 reset();read_ok=0;format_expected=C3_OUTPUT_BMP;add(6000,C3_BTN_FILM);assert(run(1,1)==6020&&hints==1&&hint_at==800&&load_count==1);
 reset();add(1000,C3_BTN_FILM);assert(run(1,1)==1020&&hints==1&&hint_at==show_at&&show_at==800);
 c3_ui_nav_t nav;c3_ui_nav_reset(&nav,0,tick);uint16_t suppress=0;uint32_t activity=0;uint8_t dial=0;
 postshot_resume_input(&nav,&suppress,&activity,&dial);assert(suppress==C3_BTN_FILM&&nav.inhibit_film&&activity==tick&&dial==3);
 /* Successful SD retry: old error text is contained in a panel; erase
  * just that panel, not the last preview. Other settings don't touch it. */
 for(unsigned review=0;review<=1;review++)for(unsigned wind=0;wind<=1;wind++){
  reset();c3_settings_t settings;c3_settings_defaults(&settings);
  if(!review)c3_settings_next(&settings,C3_SETTING_REVIEW);
  if(!wind)c3_settings_next(&settings,C3_SETTING_WIND);
  c3_lcd_rect(12,76,296,113,0xf800);
  postshot_prepare_retry_background(&settings);
  assert(fb[80][20]==(wind&&!review?0:0xf800));
  if(wind&&!review){add(1000,C3_BTN_FILM);assert(run(0,1)==1020);check_bg(1);}
 }
} 
int main(void){
 for(unsigned zh=0;zh<2;zh++){c3_i18n_set_language(zh);scenarios();}
 puts("PASS 20 bilingual foreground modal scenarios and 8 retry background gates; real i18n/icon drawing preserves every non-overlay pixel, no full-screen clear or file read in wind-only mode");
}

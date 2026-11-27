/* Test the actual foreground postshot_wait implementation extracted from
 * main.c by run_v219_tests.sh, with the real debouncer and fake time/LCD/SD. */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "c3_settings.h"
#include "c3_input.h"
#include "c3_ui_nav.h"
#include "c3_postshot.h"
#include "c3_ui_hints.h"
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
static void c3_lcd_fill(uint16_t c){(void)c;fill_count++;}
static void c3_ui_message(const char *s){assert(!strcmp(s,"SAVED"));saved_count++;}
static const char *c3_i18n_translate(const char *s){return s;}
static int c3_i18n_width(const char *s,int n){return (int)strlen(s)*6*n;}
static void c3_lcd_rect(int x,int y,int w,int h,uint16_t c){assert(x==0&&y==216&&w==320&&h==24&&c==0);}
void c3_ui_hint(int x,int y,c3_hint_key_t k,const char *s,uint16_t color){assert(x>=0&&y==220&&k==C3_HINT_FILM&&!strcmp(s,"WIND FILM LEVER")&&color==0xffff);hints++;hint_at=tick-epoch;}
#include "v219_modal.inc"
static void reset(void){epoch=tick=UINT32_MAX-500u;show_at=hint_at=0;load_count=hints=fill_count=saved_count=count=0;read_ok=1;format_expected=C3_OUTPUT_DNG;id_expected=713;}
static void add(uint32_t t,uint16_t m){assert(count<16);events[count++]=(struct event){t,m};}
static uint32_t run(unsigned review,unsigned wind){c3_settings_t s;c3_settings_defaults(&s);if(!review)c3_settings_next(&s,C3_SETTING_REVIEW);if(!wind)c3_settings_next(&s,C3_SETTING_WIND);uint8_t sd=1;assert(postshot_wait(&s,(c3_output_format_t)format_expected,&sd)==!!(review||wind));assert(sd==(review?read_ok:1));return tick-epoch;}
int main(void){
 reset();assert(run(0,0)==0&&!load_count&&!hints);
 reset();assert(run(1,0)==5800&&load_count==1&&show_at==800&&!hints);
 reset();add(1500,C3_BTN_SHUTTER);assert(run(1,0)==1520&&!hints);
 reset();add(200,C3_BTN_ISO);add(300,0);assert(run(1,0)==800&&!hints);
 reset();add(1000,C3_BTN_FILM);assert(run(0,1)==1020&&hints==1&&hint_at==0&&!load_count&&fill_count==1&&saved_count==1);
 reset();add(1000,C3_BTN_ISO);add(1100,0);add(2000,C3_BTN_SHUTTER);add(2100,0);add(6000,C3_BTN_4X);add(6100,0);add(65000,C3_BTN_FILM);assert(run(1,1)==65020&&hints==1&&hint_at==5800&&load_count==1);
 reset();add(0,C3_BTN_FILM);add(200,0);add(6100,C3_BTN_FILM);assert(run(1,1)==6120&&hints==1&&hint_at==5800);
 reset();add(200,C3_BTN_FILM);add(300,0);assert(run(1,1)==800&&!hints&&load_count==1);
 reset();read_ok=0;format_expected=C3_OUTPUT_BMP;add(6000,C3_BTN_FILM);assert(run(1,1)==6020&&hints==1&&load_count==1);
 c3_ui_nav_t nav;c3_ui_nav_reset(&nav,0,tick);uint16_t suppress=0;uint32_t activity=0;uint8_t dial=0;
 postshot_resume_input(&nav,&suppress,&activity,&dial);assert(suppress==C3_BTN_FILM&&nav.inhibit_film&&activity==tick&&dial==3);
 puts("PASS actual post-shot foreground modal: 9 scenarios, exact saved file, 5s after rendering, held and decode-time input, indefinite lever gate, read fault, final input reset");
}

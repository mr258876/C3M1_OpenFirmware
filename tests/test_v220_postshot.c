#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_postshot.h"
#include "c3_settings.h"
#include "c3_input.h"
#include "c3_ui_nav.h"
static const uint16_t buttons[]={C3_BTN_ISO,C3_BTN_SHUTTER,C3_BTN_4X,C3_BTN_FILM};
static uint16_t mask(unsigned i){uint16_t m=0;for(unsigned j=0;j<4;j++)if(i&(1u<<j))m|=buttons[j];return m;}
static void input_for(uint16_t m,uint32_t t){for(unsigned i=0;i<6;i++)c3_input_sample(m,t+5*i);}
int main(void){
 c3_settings_t s;c3_settings_defaults(&s);
 assert(c3_settings_review(&s)&&c3_settings_wind(&s)&&c3_settings_language_pending(&s));
 assert(!strcmp(c3_settings_label(C3_SETTING_REVIEW),"INSTANT REVIEW"));
 assert(!strcmp(c3_settings_label(C3_SETTING_WIND),"WIND AFTER SHOT"));
 assert(sizeof s==12&&sizeof(c3_settings_record_t)==32&&C3_SETTINGS_VERSION==6);
 unsigned n=0;
 for(unsigned code=0;code<8;code++)for(unsigned r=0;r<2;r++)for(unsigned w=0;w<2;w++){
    s.peak_area=code|(r?0:C3_POST_REVIEW_OFF)|(w?0:C3_POST_WIND_OFF);
    c3_settings_t before=s;assert(c3_settings_valid(&s));
    assert(c3_settings_review(&s)==r&&c3_settings_wind(&s)==w);
    assert(c3_settings_language(&s)==(code==4||code==6));
    assert(c3_settings_language_pending(&s)==(code==7));
    assert(c3_settings_startup_hint(&s)==(code<=4||code==7));
    assert(!strcmp(c3_settings_value(&s,C3_SETTING_REVIEW),r?"ON":"OFF"));
    assert(!strcmp(c3_settings_value(&s,C3_SETTING_WIND),w?"ON":"OFF"));
    c3_settings_record_t rec;c3_settings_make_record(&rec,&s,12);
    assert(c3_settings_record_valid(&rec)&&!memcmp(&s,&rec.data,sizeof s));
    c3_settings_next(&s,C3_SETTING_REVIEW);assert(c3_settings_review(&s)!=r&&c3_settings_wind(&s)==w);
    c3_settings_next(&s,C3_SETTING_REVIEW);assert(!memcmp(&s,&before,sizeof s));
    c3_settings_next(&s,C3_SETTING_WIND);assert(c3_settings_review(&s)==r&&c3_settings_wind(&s)!=w);
    c3_settings_next(&s,C3_SETTING_WIND);assert(!memcmp(&s,&before,sizeof s));
    c3_settings_set_ui_prefs(&s,1,0);assert(c3_settings_language(&s)&&!c3_settings_startup_hint(&s));
    assert(c3_settings_review(&s)==r&&c3_settings_wind(&s)==w);n++;
 }
 printf("PASS %u settings flag combinations: old v6 compatibility, two independent switches, language/hint preservation and CRC\n",n);
 n=0;
 for(unsigned r=0;r<2;r++)for(unsigned w=0;w<2;w++)for(unsigned b=0;b<16;b++)for(unsigned t=0;t<100;t++){
    uint32_t start=UINT32_MAX-2500u+t*67u;
    c3_postshot_t p=c3_postshot_begin(r,w,start);
    assert(p.state==(w?C3_POST_WIND:r?C3_POST_REVIEW:C3_POST_DONE));
    c3_postshot_poll(&p,0,start+4999u);assert(p.state==(w?C3_POST_WIND:r?C3_POST_REVIEW:C3_POST_DONE));
    c3_postshot_poll(&p,mask(b),start+4999u);
    unsigned done=(!r&&!w)||(w?(mask(b)&C3_BTN_FILM):mask(b));
    assert((p.state==C3_POST_DONE)==!!done);
    c3_postshot_poll(&p,0,start+5000u);assert(p.state==(!done&&w?C3_POST_WIND:C3_POST_DONE));
    c3_postshot_poll(&p,C3_BTN_ISO|C3_BTN_SHUTTER|C3_BTN_4X,start+65000u);
    assert(p.state==(!done&&w?C3_POST_WIND:C3_POST_DONE));
    c3_postshot_poll(&p,C3_BTN_FILM,start+65001u);assert(p.state==C3_POST_DONE);n++;
 }
 printf("PASS %u modal/timer cases: four switch combinations, all key masks, rollover, 5s review-only timeout, immediate indefinite wind wait\n",n);
 /* Actual debouncer: held lever on entry does NOT trigger until release and re-press.
    Long or short release, including a held shutter, does not act as fresh press. */
 for(unsigned b=0;b<16;b++){
    uint16_t m=mask(b);c3_input_reset(m,100u);
    c3_postshot_t p=c3_postshot_begin(1,1,100u);
    input_for(m,1000u);c3_input_batch_t batch=c3_input_take();assert(!batch.events.pressed);
    c3_postshot_poll(&p,batch.events.pressed,1000u);assert(p.state==C3_POST_WIND);
    input_for(0,1100u);batch=c3_input_take();assert(!batch.events.pressed);
    c3_postshot_poll(&p,batch.events.pressed,1125u);assert(p.state==C3_POST_WIND);
    input_for(C3_BTN_SHUTTER|C3_BTN_ISO|C3_BTN_4X,1200u);batch=c3_input_take();
    c3_postshot_poll(&p,batch.events.pressed,1225u);assert(p.state==C3_POST_WIND);
    input_for(C3_BTN_FILM,1300u);batch=c3_input_take();assert(batch.events.pressed&C3_BTN_FILM);
    c3_postshot_poll(&p,batch.events.pressed,1325u);assert(p.state==C3_POST_DONE);
    c3_input_reset(C3_BTN_FILM,1400u);c3_ui_nav_t nav;c3_ui_nav_reset(&nav,0,1400u);nav.inhibit_film=1;
    input_for(C3_BTN_FILM,2200u);batch=c3_input_take();assert(batch.events.long_pressed&C3_BTN_FILM);
    assert(c3_ui_nav_poll(&nav,batch.events,batch.stable,0,2225u,&s)==C3_NAV_NONE&&nav.page==C3_UI_PREVIEW);
    input_for(0,2300u);batch=c3_input_take();assert(c3_ui_nav_poll(&nav,batch.events,batch.stable,0,2325u,&s)==C3_NAV_NONE);
 }
 puts("PASS 16 held-key entry cases, actual debounce, non-film ignored, fresh FILM exits, long-hold menu inhibited on return");
 return 0;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_settings.h"
#include "c3_focus.h"
#include "c3_sleep.h"
#include "c3_startup_hint.h"

int main(void)
{
    c3_settings_t d;
    memset(&d, 0xFF, sizeof(d));
    c3_settings_defaults(&d);
    assert(c3_settings_valid(&d));
    assert(d.peaking == 1 && d.color == 0 && d.sensitivity == 2);
    assert(d.focus_score == C3_SCORE_FULL_SPARSE);
    assert(d.fps == 0 && d.grid == 0 && d.monochrome == 0 && d.osd == 1);
    assert(d.output_format == C3_OUTPUT_DNG && d.rotate_180 == 1);
    assert(d.sleep_timer == 3 && c3_sleep_timeout_ms(d.sleep_timer) == 60000u);
    assert(c3_settings_language_pending(&d) && c3_settings_startup_hint(&d));
    assert(sizeof(d)==12 && sizeof(c3_settings_record_t)==32 && C3_SETTINGS_VERSION==6);
    const struct {uint8_t item;const char *value;} expected[]={
        {C3_SETTING_PEAK,"ON"},{C3_SETTING_COLOR,"RED"},{C3_SETTING_SENSITIVITY,"HIGH"},
        {C3_SETTING_SCORE,"ON"},{C3_SETTING_FPS,"OFF"},{C3_SETTING_GRID,"OFF"},
        {C3_SETTING_MONO,"OFF"},{C3_SETTING_OSD,"ON"},{C3_SETTING_OUTPUT,"RAW DNG"},
        {C3_SETTING_ROTATION,"ON"},{C3_SETTING_SLEEP_TIMER,"1 MIN"},{C3_SETTING_STARTUP_HINT,"ON"}
    };
    for(unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i)
        assert(!strcmp(c3_settings_value(&d,expected[i].item),expected[i].value));
    c3_settings_record_t rec;
    c3_settings_make_record(&rec,&d,1);
    assert(c3_settings_record_valid(&rec));
    puts("PASS all 12 requested factory defaults, UI labels, 60s sleep, v6 pending-language record");
    unsigned count=0;
    for(unsigned lang=0;lang<2;++lang)for(unsigned prior=0;prior<3;++prior)
    for(unsigned start=0;start<1000;++start){
        c3_settings_t s=d;
        /* Existing chooser only calls this after successfully confirming language. */
        c3_settings_set_ui_prefs(&s,lang,c3_settings_startup_hint(&d));
        assert(c3_settings_language(&s)==lang&&!c3_settings_language_pending(&s));
        assert(c3_settings_startup_hint(&s));
        uint32_t t=UINT32_MAX-500u+start;
        c3_startup_hint_t hint={.first_ms=t-100000u,.state=prior};
        c3_startup_hint_arm(&hint);
        /* Time spent in picker / reinitialization and incomplete frames must not count. */
        assert(!c3_startup_hint_frame(&hint,1,0,t-30000u)&&hint.state==0);
        assert(!c3_startup_hint_frame(&hint,1,0,t-10000u)&&hint.state==0);
        assert(c3_startup_hint_frame(&hint,1,1,t)&&hint.first_ms==t);
        assert(c3_startup_hint_frame(&hint,1,1,t+4999u));
        assert(!c3_startup_hint_frame(&hint,1,1,t+5000u));
        assert(!c3_startup_hint_frame(&hint,1,1,t+9000u));
        c3_startup_hint_arm(&hint);
        assert(c3_startup_hint_frame(&hint,1,1,t+10000u));
        c3_startup_hint_dismiss(&hint);
        assert(!c3_startup_hint_frame(&hint,1,1,t+10001u));
        c3_startup_hint_arm(&hint);
        c3_settings_set_ui_prefs(&s,lang,0);
        assert(!c3_startup_hint_frame(&hint,c3_settings_startup_hint(&s),1,t+20000u));
        assert(!c3_startup_hint_frame(&hint,1,1,t+20001u));
        count++;
    }
    printf("PASS %u hint re-arm cases (2 languages, 3 previous states, tick wrap, incomplete frame, dismissal and OFF)\n",count);
}

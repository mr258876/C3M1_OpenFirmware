#include "c3_board_pins.h"
#include "c3_ui_nav.h"
#include "c3_settings.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define ISO C3_BTN_ISO
#define SHOT C3_BTN_SHUTTER
#define ZOOM C3_BTN_4X
#define FILM C3_BTN_FILM
static c3_button_events_t down(uint16_t pin)
{
    c3_button_events_t e={0};
    e.pressed=pin;
    return e;
}
static c3_button_events_t up(uint16_t pin)
{
    c3_button_events_t e={0};
    e.released=pin; e.short_released=pin;
    return e;
}
static c3_button_events_t empty(void)
{
    c3_button_events_t e={0};
    return e;
}

static c3_button_events_t held(uint16_t pin)
{
    c3_button_events_t e = {0};
    e.long_pressed = pin;
    return e;
}
static void test_menu_long_hold(void)
{
    c3_settings_t prefs;
    c3_settings_defaults(&prefs);
    c3_ui_nav_t s;
    c3_ui_nav_reset(&s,15u,100u);
    assert(s.page==C3_UI_PREVIEW);
    /* FILM tap, and even an ISO/4X chord before long hold, do not open. */
    assert(c3_ui_nav_poll(&s,down(FILM),FILM,15u,120u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,down(ISO),FILM|ISO,15u,130u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,up(ISO),FILM,15u,150u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,up(FILM),0,15u,160u,&prefs)==0u);
    assert(s.page==C3_UI_PREVIEW);
    assert(c3_ui_nav_poll(&s,down(FILM),FILM,15u,300u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,15u,600u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,held(FILM),FILM,15u,971u,&prefs)==C3_NAV_MODE_OPEN);
    assert(s.page==C3_UI_MODE);
    assert(c3_ui_nav_poll(&s,down(ISO),FILM|ISO,15u,980u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM|ISO,15u,990u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,up(ISO),FILM,15u,1000u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,down(ZOOM),FILM|ZOOM,15u,1010u,&prefs)==C3_NAV_MENU_OPEN);
    assert(s.page==C3_UI_MENU && s.selected==0u);
    /* Releasing film does NOT close the menu. */
    assert(c3_ui_nav_poll(&s,up(FILM),0,15u,1020u,&prefs)==0u);
    /* Rotary 0F -> 00 is one step forwards, only after debounce. */
    assert(c3_ui_nav_poll(&s,empty(),0,0u,1030u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),0,0u,1090u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==1u);
    /* And 00 -> 0F goes backwards. */
    assert(c3_ui_nav_poll(&s,empty(),0,15u,1100u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),0,15u,1160u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==0u);
    assert(prefs.peaking==0);
    assert(c3_ui_nav_poll(&s,down(ZOOM),ZOOM,15u,1170u,&prefs)==C3_NAV_SETTING);
    assert(prefs.peaking==1);
    assert(c3_ui_nav_poll(&s,empty(),ZOOM,15u,1190u,&prefs)==0u);
    assert(prefs.peaking==1);
    /* SHUTTER in menu exits UI; never means capture. */
    assert(c3_ui_nav_poll(&s,down(SHOT),SHOT,15u,1200u,&prefs)==C3_NAV_BACK);
    assert(s.page==C3_UI_PREVIEW);
    /* Long FILM once again, then SHOT exit with lever down inhibits retrigger. */
    assert(c3_ui_nav_poll(&s,down(FILM),FILM,15u,1300u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,held(FILM),FILM,15u,1980u,&prefs)==C3_NAV_MODE_OPEN);
    assert(c3_ui_nav_poll(&s,down(SHOT),FILM|SHOT,15u,2000u,&prefs)==C3_NAV_BACK);
    assert(s.inhibit_film);
    assert(c3_ui_nav_poll(&s,held(FILM),FILM,15u,2700u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,up(FILM),0,15u,2710u,&prefs)==0u);
    assert(!s.inhibit_film);
    assert(c3_ui_nav_poll(&s,down(FILM),FILM,15u,2800u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,held(FILM),FILM,15u,3480u,&prefs)==C3_NAV_MODE_OPEN);
    assert(c3_ui_nav_poll(&s,up(FILM),0,15u,3490u,&prefs)==0u);
    assert(s.page==C3_UI_MODE);
    assert(c3_ui_nav_poll(&s,down(ISO),ISO,15u,3500u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,down(SHOT),SHOT,15u,3520u,&prefs)==C3_NAV_BACK);
    /* A completed long hold may be latched during a blocking frame. */
    c3_button_events_t late=held(FILM);
    late.released=late.long_released=FILM;
    assert(c3_ui_nav_poll(&s,late,0,15u,5000u,&prefs)==C3_NAV_MODE_OPEN);
    assert(c3_ui_nav_poll(&s,empty(),0,15u,5100u,&prefs)==0u);
    assert(s.page==C3_UI_MODE);
    assert(c3_ui_nav_poll(&s,down(ZOOM),ZOOM,15u,5200u,&prefs)==C3_NAV_MENU_OPEN);
    assert(c3_ui_nav_poll(&s,down(SHOT),SHOT,15u,5300u,&prefs)==C3_NAV_BACK);
    assert(s.page==C3_UI_PREVIEW);
}
static void test_rotary_items(void)
{
    c3_settings_t prefs;
    c3_settings_defaults(&prefs);
    c3_ui_nav_t s;
    c3_ui_nav_reset(&s,0u,0u);
    assert(c3_ui_nav_poll(&s,down(FILM),FILM,0u,1u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,held(FILM),FILM,0u,680u,&prefs)==C3_NAV_MODE_OPEN);
    assert(c3_ui_nav_poll(&s,down(ZOOM),FILM|ZOOM,0u,690u,&prefs)==C3_NAV_MENU_OPEN);
    for (unsigned c=1;c<=8;c++) {
        assert(c3_ui_nav_poll(&s,empty(),FILM,(uint8_t)c,c*100u+700u,&prefs)==0u);
        assert(c3_ui_nav_poll(&s,empty(),FILM,(uint8_t)c,c*100u+760u,&prefs)==C3_NAV_CURSOR);
        assert(s.selected==c);
    }
    assert(s.selected==C3_SETTING_OSD && s.first_visible==2u);
    assert(c3_ui_nav_poll(&s,down(ZOOM),FILM|ZOOM,8u,1600u,&prefs)==C3_NAV_SETTING);
    assert(prefs.osd==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,9u,1620u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,9u,1680u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==C3_SETTING_OUTPUT && s.first_visible==3u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,10u,1700u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,10u,1760u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==C3_SETTING_ROTATION && s.first_visible==4u);
    assert(c3_ui_nav_poll(&s,down(ZOOM),FILM|ZOOM,10u,1770u,&prefs)==C3_NAV_SETTING);
    assert(prefs.rotate_180==0u); /* LCD rotate item toggled by 4X */
    assert(c3_ui_nav_poll(&s,empty(),FILM,11u,1800u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,11u,1860u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==C3_SETTING_BATTERY && s.first_visible==5u);
    c3_settings_t unchanged=prefs;
    assert(c3_ui_nav_poll(&s,down(ZOOM),FILM|ZOOM,11u,1870u,&prefs)==C3_NAV_CURSOR);
    assert(memcmp(&unchanged,&prefs,sizeof(prefs))==0); /* battery is read-only */
    assert(c3_ui_nav_poll(&s,empty(),FILM,12u,1900u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),FILM,12u,1960u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==C3_SETTING_SLEEP_TIMER && s.first_visible==6u);
    assert(c3_ui_nav_poll(&s,down(ZOOM),ZOOM,12u,1970u,&prefs)==C3_NAV_SETTING);
    assert(prefs.sleep_timer==1u);
    assert(c3_ui_nav_poll(&s,empty(),0,13u,1980u,&prefs)==0u);
    assert(c3_ui_nav_poll(&s,empty(),0,13u,2040u,&prefs)==C3_NAV_CURSOR);
    assert(s.selected==0u && s.first_visible==0u);
}
static void test_simultaneous_chords(void)
{
    c3_ui_nav_t n;
    c3_settings_t pref;
    c3_settings_defaults(&pref);
    c3_ui_nav_reset(&n,1u,0u);
    /* FILM+4X does NOT open menu immediately. The hold is mandatory. */
    assert(c3_ui_nav_poll(&n,down(FILM|ZOOM),FILM|ZOOM,1u,50u,&pref)==0u);
    assert(n.page==C3_UI_PREVIEW);
    assert(c3_ui_nav_poll(&n,held(FILM),FILM|ZOOM,1u,720u,&pref)==C3_NAV_MENU_OPEN);
    assert(n.page==C3_UI_MENU);
    assert(c3_ui_nav_poll(&n,empty(),FILM|ZOOM,1u,780u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,down(SHOT),FILM|SHOT,1u,790u,&pref)==C3_NAV_BACK);
    assert(n.page==C3_UI_PREVIEW && n.inhibit_film);
    c3_ui_nav_poll(&n,up(FILM),0u,1u,800u,&pref);
    assert(c3_ui_nav_poll(&n,down(FILM|ISO),FILM|ISO,1u,900u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,held(FILM),FILM|ISO,1u,1580u,&pref)==C3_NAV_MODE_OPEN);
    assert(n.page==C3_UI_MODE);
    /* ISO already down at hold threshold does not cycle the mode. */
    assert(c3_ui_nav_poll(&n,empty(),FILM|ISO,1u,1590u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,up(ISO),FILM,1u,1600u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,down(ISO),FILM|ISO,1u,1610u,&pref)==0u);
}

static void test_mode_ev_rotary(void)
{
    c3_settings_t pref;
    c3_settings_defaults(&pref);
    c3_ui_nav_t n;
    c3_ui_nav_reset(&n,15u,0u);
    assert(c3_ui_nav_poll(&n,held(FILM),FILM,15u,700u,&pref)==C3_NAV_MODE_OPEN);
    assert(n.ev_step_delta==0);
    /* Debounce: EV mapping reversed vs raw movement, including wrap. */
    assert(c3_ui_nav_poll(&n,empty(),FILM,0u,710u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,empty(),FILM,0u,770u,&pref)==C3_NAV_EV_ADJUST);
    assert(n.ev_step_delta==-1);
    assert(c3_ui_nav_poll(&n,empty(),FILM,15u,780u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,empty(),FILM,15u,840u,&pref)==C3_NAV_EV_ADJUST);
    assert(n.ev_step_delta==1);
    /* Moving two positions is two 0.5EV steps. */
    assert(c3_ui_nav_poll(&n,empty(),FILM,1u,850u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,empty(),FILM,1u,910u,&pref)==C3_NAV_EV_ADJUST);
    assert(n.ev_step_delta==-2);
    /* Ev rotary must NOT change navigation cursor or settings. */
    assert(n.selected==0 && pref.peaking==0);
    assert(c3_ui_nav_poll(&n,down(ISO),FILM|ISO,1u,920u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,down(ZOOM),FILM|ZOOM,1u,930u,&pref)==C3_NAV_MENU_OPEN);
    assert(n.page==C3_UI_MENU);
    assert(c3_ui_nav_poll(&n,empty(),FILM,2u,940u,&pref)==0u);
    assert(c3_ui_nav_poll(&n,empty(),FILM,2u,1000u,&pref)==C3_NAV_CURSOR);
    assert(n.selected==1u);
    puts("MODE rotary: 16-state wrap, 60ms debounce, 0.5EV delta, MENU isolation PASS");
}

static void test_settings_record(void)
{
    c3_settings_t prefs;
    c3_settings_defaults(&prefs);
    assert(c3_settings_valid(&prefs));
    assert(prefs.peaking==0u && prefs.sensitivity==1u && prefs.osd==1u);
    assert(prefs.output_format==C3_OUTPUT_DNG);
    assert(prefs.rotate_180==1u); /* new installs start rotated */
    assert(prefs.peak_area==C3_PEAK_AREA_CENTER);
    assert(prefs.sleep_timer==0u);
    assert(strcmp(c3_settings_value(&prefs,C3_SETTING_SLEEP_TIMER),"OFF")==0);
    static const char *names[]={"15 SEC","30 SEC","1 MIN","3 MIN","OFF"};
    for (unsigned i=0;i<5;i++) {
        c3_settings_next(&prefs,C3_SETTING_SLEEP_TIMER);
        assert(strcmp(c3_settings_value(&prefs,C3_SETTING_SLEEP_TIMER),names[i])==0);
    }
    prefs.sleep_timer=5u;
    assert(!c3_settings_valid(&prefs));
    prefs.sleep_timer=0u;
    static const uint8_t areas[]={C3_PEAK_AREA_CENTER_SPARSE,C3_PEAK_AREA_FULL,
        C3_PEAK_AREA_FULL_SPARSE,C3_PEAK_AREA_CENTER};
    static const char *area_names[]={"CENTER SP","FULL","FULL SP","CENTER"};
    for (unsigned i=0;i<4;i++) {
        c3_settings_next(&prefs,C3_SETTING_PEAK_AREA);
        assert(prefs.peak_area==areas[i] && c3_settings_valid(&prefs));
        assert(strcmp(c3_settings_value(&prefs,C3_SETTING_PEAK_AREA),area_names[i])==0);
    }
    prefs.peak_area=C3_PEAK_AREA_COUNT;
    assert(!c3_settings_valid(&prefs));
    prefs.peak_area=C3_PEAK_AREA_CENTER;
    c3_settings_next(&prefs,C3_SETTING_ROTATION);
    assert(prefs.rotate_180==0u && c3_settings_valid(&prefs));
    c3_settings_next(&prefs,C3_SETTING_ROTATION);
    assert(prefs.rotate_180==1u);
    c3_settings_next(&prefs,C3_SETTING_OUTPUT);
    assert(prefs.output_format==C3_OUTPUT_BMP && c3_settings_valid(&prefs));
    c3_settings_next(&prefs,C3_SETTING_OUTPUT);
    assert(prefs.output_format==C3_OUTPUT_DNG);
    assert(c3_settings_color565(&prefs)==0xF800u);
    for (unsigned i=0;i<C3_SETTINGS_COUNT;i++) {
        assert(strlen(c3_settings_label(i)));
        assert(strlen(c3_settings_value(&prefs,i)));
    }
    c3_settings_next(&prefs,C3_SETTING_COLOR);
    assert(prefs.color==1 && c3_settings_color565(&prefs)==0xFFE0u);
    c3_settings_next(&prefs,C3_SETTING_COLOR);
    assert(prefs.color==2 && c3_settings_color565(&prefs)==0x07FFu);
    c3_settings_next(&prefs,C3_SETTING_SENSITIVITY);
    assert(prefs.sensitivity==2);
    assert(c3_settings_peak_threshold(&prefs,20u)==12u);
    c3_settings_next(&prefs,C3_SETTING_SENSITIVITY);
    assert(prefs.sensitivity==0u);
    assert(c3_settings_peak_threshold(&prefs,20u)==28u);
    c3_settings_record_t a={0},b={0};
    assert(sizeof(a)==32u);
    c3_settings_make_record(&a,&prefs,0xFFFFFFFFu);
    assert(c3_settings_record_valid(&a));
    assert(c3_settings_select_record(&a,&b)==0u);
    c3_settings_make_record(&b,&prefs,0u);
    assert(c3_settings_select_record(&a,&b)==1u); /* seq wrap */
    b.commit = 0xFFFFu; /* sudden power loss before commit */
    assert(!c3_settings_record_valid(&b));
    assert(c3_settings_select_record(&a,&b)==0u);
    b=a;
    b.data.peaking^=1;
    assert(!c3_settings_record_valid(&b)); /* CRC detects corruption */
    b=a;
    b.crc^=0x8000u;
    assert(!c3_settings_record_valid(&b));
    a.commit=0;
    assert(c3_settings_select_record(&a,&b)==2u);
    puts("Menu settings: CRC/rollback, 16-code rotary, FILM long hold, mode/menu navigation PASS");
}
/* 180-degree user preference survives a versioned flash-record round-trip. */
static void test_rotation_record(void)
{
    c3_settings_t prefs;
    c3_settings_defaults(&prefs);
    assert(C3_SETTINGS_VERSION==6u);
    assert(sizeof(c3_settings_t)==12u);
    assert(sizeof(c3_settings_record_t)==32u);
    c3_settings_record_t record;
    c3_settings_make_record(&record,&prefs,19u);
    assert(c3_settings_record_valid(&record));
    assert(record.data.rotate_180==1u);
    record.data.rotate_180=0u;
    assert(!c3_settings_record_valid(&record)); /* CRC protects new field */
    c3_settings_t changed = record.data;
    c3_settings_make_record(&record,&changed,20u);
    assert(c3_settings_record_valid(&record));
    assert(record.data.rotate_180==0u);
    c3_settings_defaults(&changed);
    changed.peak_area=C3_PEAK_AREA_FULL;
    c3_settings_make_record(&record,&changed,21u);
    assert(c3_settings_record_valid(&record));
    record.data.peak_area=C3_PEAK_AREA_CENTER;
    assert(!c3_settings_record_valid(&record));
    record.data.sleep_timer=4u;
    assert(!c3_settings_record_valid(&record)); /* CRC protects sleep setting */
    c3_settings_t sleep_pref=record.data;
    c3_settings_make_record(&record,&sleep_pref,22u);
    assert(c3_settings_record_valid(&record) && record.data.sleep_timer==4u);
    puts("LCD rotation, peak area and sleep menu: defaults, CRC persistence PASS");
}

int main(void)
{
    test_rotation_record();
    test_menu_long_hold();
    test_rotary_items();
    test_simultaneous_chords();
    test_mode_ev_rotary();
    test_settings_record();
    return 0;
}

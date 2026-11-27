/* Register-level regression: real exposure functions, simulated retained
 * vertical dummy rows and dropped/failed SCCB transactions. Not a sensor model. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "c3_exposure.h"
static const uint8_t vregs[]={0x0e,0x2e,0x2d,0x34,0x33};
static uint8_t reg[256];
static int blocked=-1, badread=-1, failwrite=-1;
static int drop_once=-1;
static unsigned vwrite, reads;
static uint8_t write_reg(uint8_t a,uint8_t v){
    for (unsigned i=0;i<sizeof(vregs);++i) if(a==vregs[i]) {
        ++vwrite;
        assert(!(reg[0x13]&5)); /* auto off before altering frame timing */
        if(a!=0x0e) assert(!(reg[0x0e]&0x80));
    }
    if(a==failwrite)return 1;
    if(a==drop_once){drop_once=-1;return 0;}
    if(a!=blocked)reg[a]=v;
    return 0;
}
static uint8_t read_reg(uint8_t a,uint8_t *v){++reads;if(a==badread)return 0;*v=reg[a];return 1;}
static void seed_night(void){
    memset(reg,0,sizeof(reg));reg[0x0e]=0xf5;reg[0x13]=0xff;
    reg[0x2d]=0x91;reg[0x2e]=7;reg[0x33]=0xd2;reg[0x34]=2;
    reg[0x10]=0x8f;reg[0x08]=8;reg[0x1f]=8;
    reg[0x00]=0xf0;reg[0x2a]=0x10;reg[0x2b]=0x80;reg[0x11]=0;
    /* Unrelated BLC, AWB state must survive. */
    reg[0x3e]=0xe2;reg[0x42]=0x7f;reg[0x01]=0xa8;reg[0x02]=0x80;reg[0x03]=0x40;
    blocked=badread=failwrite=drop_once=-1;vwrite=reads=0;
}
static void assert_clean(void){for(unsigned i=0;i<sizeof(vregs);++i)assert(reg[vregs[i]]==0);}
static void assert_color(void){assert(reg[0x3e]==0xe2&&reg[0x42]==0x7f&&reg[0x01]==0xa8&&reg[0x02]==0x80&&reg[0x03]==0x40);}
int main(void){
    unsigned n=0;
    for(unsigned z=0;z<2;++z)for(unsigned d=0;d<9;++d)for(unsigned g=0;g<6;++g)for(unsigned m=0;m<3;++m){
        c3_exposure_t s; seed_night();
        c3_exposure_set_preview_zoom(z);c3_exposure_init(&s,9,0);
        c3_exposure_sync_dial(&s,d,0);for(unsigned i=0;i<g;++i)c3_exposure_gain_next(&s);
        assert(c3_exposure_prepare_timing(&s,m,read_reg,write_reg));assert_clean();
        assert(c3_exposure_apply(&s,m,write_reg));assert_clean();assert_color();
        assert(reg[0x1f]==0 && !(reg[0x13]&1));
        assert((reg[0x13]&4)==(g?0:4));
        assert(((unsigned)reg[0x08]*256u+reg[0x10])==c3_exposure_rows_for_mode(d,m,0));
        if(g)assert(reg[0x00]==c3_exposure_gain_reg(g));
#if C3_SHUTTER_DYNAMIC_TIMING
        c3_shutter_plan_t p;assert(c3_shutter_plan(m==C3_EXPOSURE_RAW?C3_TIMING_RAW8:(m==C3_EXPOSURE_PREVIEW&&z?C3_TIMING_RGB4X:C3_TIMING_RGB1X),d,1,&p));
        assert(reg[0x11]==p.clkrc&&reg[0x2a]==(uint8_t)((p.dummy>>8)<<4)&&reg[0x2b]==(uint8_t)p.dummy);
#endif
        /* Go back to P; automatic night-mode behavior remains enabled. */
        c3_exposure_sync_dial(&s,9,1);assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,read_reg,write_reg));
        assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,write_reg));assert(reg[0x0e]==0xf5&&(reg[0x13]&5)==5);++n;
    }
    printf("PASS %u P -> S/M -> P mode/zoom/shutter/gain/format cases\n",n);
    /* P preparation must not reset its existing vertical rows, also for P RAW. */
    for(unsigned m=0;m<3;++m){c3_exposure_t s;seed_night();c3_exposure_init(&s,9,0);assert(c3_exposure_prepare_timing(&s,m,read_reg,write_reg));assert(vwrite==0&&reg[0x2d]==0x91&&reg[0x33]==0xd2);}
    /* A persistent dropped write, failed read or reported write failure must
     * reject the timing plan. A single dropped write must be retried. */
    const uint8_t failregs[]={0x13,0x0e,0x2e,0x2d,0x34,0x33};
    c3_exposure_t s;c3_exposure_init(&s,0,0);
    for(unsigned i=0;i<sizeof(failregs);++i){uint8_t a=failregs[i];
        seed_night();blocked=a;assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,read_reg,write_reg));
        seed_night();badread=a;assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,read_reg,write_reg));
        seed_night();failwrite=a;assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,read_reg,write_reg));
        seed_night();drop_once=a;assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,read_reg,write_reg));assert_clean();
    }
    puts("PASS P timing untouched; 24 SCCB fault injection / retry cases");
}

/* Real exposure and DNG code with a simulated SCCB register map; not an
 * electrical, ISP-convergence or frame-timing model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_exposure.h"
#include "c3_dng.h"
static uint8_t reg[256];
static int blocked=-1,badread=-1,badwrite=-1,drop_once=-1;
static uint8_t checking;
static uint8_t wr(uint8_t a,uint8_t v){
    if(checking){
        if(a==0x0e)assert(v==0);
        if(a==0x13)assert((v&7u)==0);
        if(a==0x2d||a==0x2e||a==0x33||a==0x34)assert(!(reg[0x13]&5)&&!(reg[0x0e]&0x80));
        if(a==0x08||a==0x10||a==0x00||a==0x02||a==0x03||a==0x01)assert(!(reg[0x13]&7));
    }
    if(a==badwrite)return 1u;
    if(a==drop_once){drop_once=-1;return 0u;}
    if(a!=blocked)reg[a]=v;
    return 0u;
}
static uint8_t rd(uint8_t a,uint8_t *v){if(a==badread)return 0;*v=reg[a];return 1;}
static unsigned word(unsigned hi,unsigned lo){return ((unsigned)reg[hi]<<8)|reg[lo];}
static void seed(unsigned rows,unsigned gain){
    memset(reg,0,sizeof(reg));
    reg[0x13]=0xff;reg[0x0e]=0xf5;reg[0x2d]=0x19;reg[0x2e]=1;reg[0x33]=0x2a;reg[0x34]=1;
    reg[0x08]=(uint8_t)(rows>>8);reg[0x10]=(uint8_t)rows;reg[0x00]=(uint8_t)gain;
    reg[0x01]=0xa0;reg[0x02]=0x60;reg[0x03]=0x64;reg[0x3e]=0xe2;reg[0x42]=0x80;
    reg[0x1f]=0x4;reg[0x2a]=0x10;reg[0x2b]=0xff;reg[0x11]=0x40;
    checking=0;blocked=badread=badwrite=drop_once=-1;
}
static void check_apply(const c3_zoom_hold_t *h,unsigned mode){
    seed(1,0x22); checking=1;
    c3_shutter_plan_t p;
    c3_timing_format_t fmt=mode==C3_EXPOSURE_RAW?C3_TIMING_RAW8:mode==C3_EXPOSURE_BMP?C3_TIMING_RGB1X:C3_TIMING_RGB4X;
    assert(c3_shutter_plan(fmt,h->dial,h->dial!=0xff,&p));
    assert(c3_zoom_hold_apply(h,(c3_exposure_sensor_mode_t)mode,rd,wr));
    unsigned rows=word(0x08,0x10);
    assert(rows==c3_zoom_hold_rows(h,(c3_exposure_sensor_mode_t)mode));
    assert(reg[0x0e]==0 && word(0x2e,0x2d)==0 && !(reg[0x13]&7) && reg[0x1f]==0);
    assert(reg[0x00]==h->source.gain_register && reg[0x11]==p.clkrc && (((unsigned)reg[0x2a]>>4)*256+reg[0x2b])==p.dummy);
    if(h->dial!=0xff){assert(rows==p.rows && word(0x34,0x33)==0);}
    else {
        c3_shutter_plan_t src;assert(c3_shutter_plan(C3_TIMING_RGB1X,0xff,0,&src));src.rows=h->source.preview_rows;
        unsigned source=c3_shutter_exposure_ns(C3_TIMING_RGB1X,&src),line=c3_shutter_row_ns(fmt,&p);
        unsigned dst=rows*line,delta=dst>source?dst-source:source-dst;
        assert(delta<=line);assert(word(0x34,0x33)==(rows>(mode==C3_EXPOSURE_RAW?476u:236u)?rows-(mode==C3_EXPOSURE_RAW?476u:236u):0u));
    }
    if(mode!=C3_EXPOSURE_RAW)assert(reg[0x02]==h->red&&reg[0x03]==h->green&&reg[0x01]==h->blue);
    assert(reg[0x3e]==0xe2&&reg[0x42]==0x80);
}
int main(void){
    c3_zoom_hold_t h;c3_exposure_t s;unsigned n=0;
    for(unsigned d=0;d<9;d++)for(unsigned g=0;g<6;g++)for(int ev=-6;ev<=6;ev++){
        seed(84,0x7f); c3_exposure_init(&s,d,0);for(unsigned k=0;k<g;k++)c3_exposure_gain_next(&s);s.ev_half_steps=(int8_t)ev;
        assert(c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_hold_matches(&h,&s));
        assert(h.source.gain_register==(g?c3_exposure_gain_reg(g):0x7f));
        for(unsigned mode=0;mode<3;mode++){check_apply(&h,mode);n++;}
        c3_exposure_sync_dial(&s,(uint8_t)((d+1)%9),0);assert(!c3_zoom_hold_matches(&h,&s));
    }
    const unsigned p_rows[]={1,2,17,84,236,255,256,277,278,300,476,510,750,1000,1500,2000,2224,3000,4000,4500};
    for(unsigned i=0;i<sizeof(p_rows)/sizeof(p_rows[0]);i++)for(unsigned g=0;g<255;g+=7){
        seed(p_rows[i],g);c3_exposure_init(&s,9,0);assert(c3_zoom_hold_read(&h,&s,rd,wr));
        assert(c3_zoom_hold_matches(&h,&s));c3_exposure_sync_dial(&s,15,0);assert(c3_zoom_hold_matches(&h,&s));
        for(unsigned mode=0;mode<3;mode++){check_apply(&h,mode);n++;}
        s.ev_half_steps++;assert(!c3_zoom_hold_matches(&h,&s));
    }
    printf("PASS %u lock exposure cases (P/S/M x 1X source x VGA/RAW/BMP destinations), fixed vertical rows and WB\n",n);
    seed(0,0);c3_exposure_init(&s,9,0);assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);
    seed(8191,0);assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);
    seed(84,255);assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);
    seed(84,0);reg[0x02]=255;assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);
    seed(84,0);blocked=0x13;assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);
    const uint8_t snapshot_regs[]={0x13,0x08,0x10,0x00,0x02,0x03,0x01};
    for(unsigned i=0;i<sizeof(snapshot_regs);i++){seed(84,0x7f);badread=snapshot_regs[i];assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&!h.source.valid);}
    seed(1500,0x7f);assert(c3_zoom_hold_read(&h,&s,rd,wr));
    const uint8_t apply_regs[]={0x13,0x0e,0x2d,0x2e,0x33,0x34,0x2a,0x2b,0x11,0x1f,0x08,0x10,0x00,0x01,0x02,0x03};
    for(unsigned i=0;i<sizeof(apply_regs);i++){
        seed(1,0x22);badread=apply_regs[i];assert(!c3_zoom_hold_apply(&h,C3_EXPOSURE_PREVIEW,rd,wr));
        seed(1,0x22);badwrite=apply_regs[i];assert(!c3_zoom_hold_apply(&h,C3_EXPOSURE_PREVIEW,rd,wr));
        seed(1,0x22);drop_once=apply_regs[i];assert(c3_zoom_hold_apply(&h,C3_EXPOSURE_PREVIEW,rd,wr));
    }
    /* Final check catches a post-apply change, even if prior clear passed. */
    for(unsigned g=0;g<6;g++){
        seed(84,0x7f);c3_exposure_init(&s,7,0);for(unsigned k=0;k<g;k++)c3_exposure_gain_next(&s);
        c3_exposure_set_preview_zoom(1);assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,rd,wr));assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,wr));
        assert(c3_exposure_verify_manual_night(&s,rd));reg[0x2d]=1;assert(!c3_exposure_verify_manual_night(&s,rd));reg[0x2d]=0;reg[0x0e]=0xf5;assert(!c3_exposure_verify_manual_night(&s,rd));
    }
    printf("PASS 12 snapshot fault/rejection cases, 48 SCCB apply failure/retry cases, 18 final manual checks\n");
    /* DNG metadata for an identical AWB is exactly equal with auto on vs
     * an explicitly held triple. Also check invalid WB falls back. */
    uint8_t a[C3_DNG_HEADER_BYTES],b[C3_DNG_HEADER_BYTES],ta[C3_DNG_TAIL_BYTES],tb[C3_DNG_TAIL_BYTES];
    seed(84,0x7f);assert(c3_dng_capture_preview_awb(rd));c3_dng_make_header(a);c3_dng_make_tail(ta);
    reg[0x13]=0xf8;assert(c3_dng_capture_locked_awb(reg[0x02],reg[0x03],reg[0x01]));c3_dng_make_header(b);c3_dng_make_tail(tb);
    assert(!memcmp(a,b,sizeof a)&&!memcmp(ta,tb,sizeof ta));
    assert(!c3_dng_capture_locked_awb(0xff,0x60,0xa0)&&!c3_dng_wb_sensor_valid());
    puts("PASS held AWB DNG header/tail byte-identical; invalid WB fallback");
}

/* Reuse actual v204 register-map test fixtures; distinguish failures without
 * changing the production checks. No claim of physical SCCB or ISP modelling. */
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
static void seed(unsigned rows,unsigned gain){
    memset(reg,0,sizeof(reg));
    reg[0x13]=0xff;reg[0x0e]=0xf5;reg[0x2d]=0x19;reg[0x2e]=1;reg[0x33]=0x2a;reg[0x34]=1;
    reg[0x08]=(uint8_t)(rows>>8);reg[0x10]=(uint8_t)rows;reg[0x00]=(uint8_t)gain;
    reg[0x01]=0xa0;reg[0x02]=0x60;reg[0x03]=0x64;reg[0x3e]=0xe2;reg[0x42]=0x80;
    reg[0x1f]=0x4;reg[0x2a]=0x10;reg[0x2b]=0xff;reg[0x11]=0x40;
    checking=0;blocked=badread=badwrite=drop_once=-1;
}

static unsigned samples;
static uint8_t drift_rd(uint8_t a,uint8_t *v){
    if(!rd(a,v))return 0;
    if(a==0x00 && ++samples==2)*v^=1u;
    return 1;
}
static uint8_t second_gain_fail(uint8_t a,uint8_t *v){
    if(a==0x00 && ++samples==2)return 0;
    return rd(a,v);
}
static uint8_t wb_drift_rd(uint8_t a,uint8_t *v){
    if(!rd(a,v))return 0;
    if(a==0x02 && ++samples>=2)*v^=1u;
    return 1;
}
int main(void){
    c3_zoom_hold_t h;c3_exposure_t s;c3_exposure_init(&s,9,0);
    seed(84,0x7f);assert(c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==0);
    seed(84,0x7f);blocked=0x13;assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==11);
    seed(0,0x7f);assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==12);
    seed(84,0x7f);badread=0x02;assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==13);
    seed(84,0x7f);samples=0;assert(!c3_zoom_hold_read(&h,&s,second_gain_fail,wr)&&c3_zoom_read_failure==14);
    seed(84,0x7f);samples=0;assert(!c3_zoom_hold_read(&h,&s,drift_rd,wr)&&c3_zoom_read_failure==15);
    seed(84,0x7f);reg[0x02]=0x3f;assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==16);
    seed(84,0x7f);samples=0;assert(!c3_zoom_hold_read(&h,&s,wb_drift_rd,wr)&&c3_zoom_read_failure==17);
    seed(8191,0x7f);assert(!c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==18);
    seed(84,0x7f);assert(c3_zoom_hold_read(&h,&s,rd,wr)&&c3_zoom_read_failure==0);
    puts("PASS eight distinct snapshot error reasons and successful reset");
    return 0;
}

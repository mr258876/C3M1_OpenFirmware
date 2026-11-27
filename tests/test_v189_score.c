/* Independent oracle for the full-frame and sparse streaming score, plus
 * exact rendering order checks on the production SRAM row compositors. */
#include "c3_focus.h"
#include "c3_settings.h"
#include "c3_preview_score.h"
#include "c3_preview_peak_center.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static uint16_t pixels[240][320];
static unsigned rng=1u;
static unsigned rand32(void) {rng=rng*1664525u+1013904223u; return rng;}
static unsigned luma(uint16_t p) {
    unsigned r=((p>>11)&31u)*2u,g=(p>>5)&63u,b=(p&31u)*2u;
    return (r*2u+g*5u+b)/8u;
}
static unsigned gradient(unsigned x,unsigned y) {
    int a=(int)luma(pixels[y][x]);
    return (unsigned)(abs(a-(int)luma(pixels[y][x-1]))+abs(a-(int)luma(pixels[y-1][x])));
}
static void frame(unsigned kind) {
    for(unsigned y=0;y<240;y++) for(unsigned x=0;x<320;x++) {
        unsigned in=(x>=80&&x<240&&y>=60&&y<180);
        if(kind==0) pixels[y][x]=0x8410u;
        else if(kind==1) pixels[y][x]=((x/7+y/9)&1u)?0xffff:0;
        else if(kind==2) pixels[y][x]=(!in&&x<60&&((x/3+y/5)&1u))?0xffff:0;
        else if(kind==3) pixels[y][x]=(uint16_t)(rand32()>>16);
        else if(kind==4) pixels[y][x]=(uint16_t)((x/10)*0x0841u);
        else pixels[y][x]=((y&1u)&&(x&1u))?0xffff:0;
    }
}
static void packrow(uint8_t row[640],unsigned y) {
    for(unsigned x=0;x<320;x++) {row[2*x]=(uint8_t)(pixels[y][x]>>8); row[2*x+1]=(uint8_t)pixels[y][x];}
}
static void oracle(unsigned mode,uint32_t *sum,uint32_t *count) {
    *sum=*count=0;
    if(!mode)return;
    unsigned center=mode==1||mode==2,step=(mode==2||mode==4)?C3_SCORE_SPARSE_STEP:1;
    for(unsigned y=center?60:1;y<(center?180:240);y+=step)
        for(unsigned x=center?80:1;x<(center?240:320);x+=step) {
            *sum+=gradient(x,y);++*count;
        }
}
static unsigned score_for(uint32_t sum,uint32_t count) {
    if(!count)return 0;
    unsigned s=(sum+count/2)/count*4u;return s>99?99:s;
}
static uint16_t ref_overlay(uint16_t p,unsigned x,unsigned y,unsigned mono,unsigned grid,unsigned center,const uint8_t *mask) {
    if(mono)p=c3_effect_gray565(p);
    if(mask) p=c3_effect_dim565(p);
    if(grid&&(y==80||y==160||x==106||x==213))p=c3_effect_grid565(p);
    if(center&&c3_focus_roi_corner(x,y))p=0x07e0;
    if(mask&&(mask[x/8]&(0x80u>>(x%8))))p=0xffff;
    return p;
}
int main(void) {
    const unsigned expected_count[]={0,19200,1200,319*239,4800};
    const char *names[]={"OFF","CENTER","CENTER SP","FULL","FULL SP"};
    c3_settings_t settings;
    c3_settings_defaults(&settings);
    assert(settings.focus_score==0);
    for(unsigned m=0;m<5;m++) {
        assert(settings.focus_score==m&&c3_settings_valid(&settings));
        assert(!strcmp(c3_settings_value(&settings,C3_SETTING_SCORE),names[m]));
        c3_settings_record_t record;
        c3_settings_make_record(&record,&settings,42);
        assert(c3_settings_record_valid(&record)&&record.data.focus_score==m);
        c3_settings_next(&settings,C3_SETTING_SCORE);
    }
    assert(settings.focus_score==0);settings.focus_score=5;assert(!c3_settings_valid(&settings));
    unsigned cases=0,composition_rows=0;
    c3_score_t s;
    c3_score_reset(&s);
    for(unsigned kind=0;kind<6;kind++) {
        frame(kind);
        for(unsigned mode=0;mode<5;mode++) {
            uint32_t sum,count;oracle(mode,&sum,&count);
            assert(count==expected_count[mode]);
            for(unsigned peak=0;peak<3;peak++) {
                c3_focus_t f,ref;
                c3_focus_reset(&f);c3_focus_reset(&ref);
                c3_focus_begin_frame(&f);c3_focus_begin_frame(&ref);
                c3_score_reset(&s);c3_score_begin_frame(&s,mode);
                for(unsigned y=0;y<240;y++) {
                    uint8_t row[640],unmodified[640];packrow(row,y);memcpy(unmodified,row,640);
                    if(!(peak&&mode==C3_SCORE_CENTER)) c3_score_sample_row(&s,y,row);
                    assert(!memcmp(row,unmodified,640));
                    if(peak==1) {
                        c3_effect_peak_line_center(row,y,&f,24,0xf800,0,0,c3_score_is_center(mode),NULL);
                        c3_effect_peak_line_center(unmodified,y,&ref,24,0xf800,0,0,c3_score_is_center(mode),NULL);
                    } else if(peak==2) {
                        c3_effect_peak_line_staged(row,y,&f,24,0xf800,0,0,c3_score_is_center(mode),NULL);
                        c3_effect_peak_line_staged(unmodified,y,&ref,24,0xf800,0,0,c3_score_is_center(mode),NULL);
                    }
                    if(peak)assert(!memcmp(row,unmodified,640));
                }
                if(peak&&mode==1)c3_score_end_shared_center(&s,f.roi_sum);
                else c3_score_end_frame(&s);
                assert(s.sum==sum&&s.count==count&&s.score==score_for(sum,count));
                if(peak) {
                    c3_focus_end_frame(&f);c3_focus_end_frame(&ref);
                    assert(f.threshold==ref.threshold&&f.roi_sum==ref.roi_sum);
                }
                if(kind==2)assert(mode==3||mode==4?s.score>0:s.score==0);
                cases++;
            }
        }
    }
    /* Smoothing is applied in the selected mode only. Switching mode/OFF
     * must reset stale scores and not reuse the previous mode's luma rows. */
    c3_score_reset(&s);frame(0);c3_score_begin_frame(&s,3);
    for(unsigned y=0;y<240;y++){uint8_t row[640];packrow(row,y);c3_score_sample_row(&s,y,row);}
    c3_score_end_frame(&s);assert(s.score==0);
    frame(1);c3_score_begin_frame(&s,3);
    for(unsigned y=0;y<240;y++){uint8_t row[640];packrow(row,y);c3_score_sample_row(&s,y,row);}
    c3_score_end_frame(&s);assert(s.score==(score_for(s.sum,s.count)+2)/4);
    c3_score_begin_frame(&s,4);assert(!s.settled&&!s.score&&!s.sum&&!s.count);
    for(unsigned y=0;y<240;y++){uint8_t row[640];packrow(row,y);c3_score_sample_row(&s,y,row);}
    c3_score_end_frame(&s);assert(s.score==score_for(s.sum,s.count));
    c3_score_begin_frame(&s,0);assert(!s.settled&&!s.score);
    c3_score_begin_frame(&s,99);assert(!s.mode);
    frame(3);
    for(unsigned mode=1;mode<5;mode++) for(unsigned mono=0;mono<2;mono++)
    for(unsigned grid=0;grid<2;grid++) for(unsigned osd=0;osd<2;osd++) {
        for(unsigned y=0;y<240;y++) {
            uint8_t row[640],bits[40];packrow(row,y);
            for(unsigned i=0;i<40;i++)bits[i]=(uint8_t)(rand32()>>24);
            const uint8_t *mask=(osd&&(y<24||y>=216))?bits:NULL;
            c3_effect_score_line(row,y,mono,grid,c3_score_is_center(mode),mask);
            for(unsigned x=0;x<320;x++)
                assert(c3_effect_get565(row,x)==ref_overlay(pixels[y][x],x,y,mono,grid,c3_score_is_center(mode),mask));
            composition_rows++;
        }
    }
    printf("PASS: %u score frame combinations, exact counts 19200/1200/76241/4800; %u row compositor cases; settings/CRC/reset/smoothing/peak isolation\n",cases,composition_rows);
    return 0;
}

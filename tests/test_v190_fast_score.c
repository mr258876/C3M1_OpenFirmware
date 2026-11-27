/* Production luma LUT, sparse row schedule, held-score frames and pixel
 * equivalence to the FIFO fast path. Also exercises exact neighbor seeding. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_focus.h"
#include "c3_preview_score.h"
#include "c3_preview_peak_center.h"
static uint8_t raw[640];
static unsigned cursor;
static uint8_t fifo(void) {assert(cursor<sizeof(raw));return raw[cursor++];}
#define C3_ROW_FIFO_BYTE() fifo()
#define C3_ROW_DIM565(c) c3_effect_dim565(c)
#include "c3_preview_row_opt.h"
static uint32_t rng=0xF170B32u;
static uint32_t random32(void) {rng=rng*1664525u+1013904223u;return rng;}
static unsigned reference_luma(unsigned p) {return (4*((p>>11)&31)+5*((p>>5)&63)+2*(p&31))/8;}
static void row(unsigned y) {
    for(unsigned x=0;x<320;x++) c3_effect_put565(raw,x,(uint16_t)(random32()^(x*39+y*37)));
}
static void oracle_overlay(uint8_t *out,unsigned y,unsigned mono,unsigned grid,unsigned center,const uint8_t *mask) {
    for(unsigned x=0;x<320;x++) {
        uint16_t c=c3_effect_get565(raw,x);
        if(mono)c=c3_effect_gray565(c);
        if(mask)c=c3_effect_dim565(c);
        if(grid&&(y==80||y==160||x==106||x==213))c=c3_effect_grid565(c);
        if(center&&c3_focus_roi_corner(x,y))c=0x07e0;
        if(c3_effect_glyph(mask,x))c=0xffff;
        c3_effect_put565(out,x,c);
    }
}
/* Exact score-OFF renderer structure in c3_camera_preview_draw, then patch
 * central corners. Actual FIFO copy helpers are the production functions. */
static void fast_overlay(uint8_t *out,unsigned y,unsigned mono,unsigned grid,unsigned center,const uint8_t *mask) {
    cursor=0;
    if(!mono) {
        if(mask)c3_row_copy_osd(out,mask);else c3_row_copy_plain(out);
        if(grid)c3_effect_grid_row(out,y,mask);
    } else {
        for(unsigned x=0;x<320;x++) {
            uint16_t hi=fifo();uint16_t c=c3_effect_gray565((uint16_t)(hi<<8|fifo()));
            if(mask)c=c3_effect_dim565(c);
            if(grid&&(y==80||y==160||x==106||x==213))c=c3_effect_grid565(c);
            if(c3_effect_glyph(mask,x))c=0xffff;
            c3_effect_put565(out,x,c);
        }
    }
    assert(cursor==640);
    if(center)c3_effect_score_corners(out,y);
}
int main(void) {
    for(unsigned p=0;p<65536;p++) assert(c3_score_luma_bytes((uint8_t)(p>>8),(uint8_t)p)==reference_luma(p));
    const unsigned expected_rows[]={0,121,60,240,120};
    const unsigned expected_samples[]={0,19200,1200,76241,4800};
    unsigned frames=0,rows=0;
    for(unsigned mode=0;mode<5;mode++) {
        unsigned count=0;
        for(unsigned y=0;y<240;y++) {
            unsigned active=c3_score_row_active(mode,y);
            unsigned ref=0;
            if(mode) {
                unsigned center=mode==1||mode==2;
                unsigned step=mode==2||mode==4?4:1;
                for(unsigned r=center?60:1;r<(center?180:240);r+=step) if(y==r||y==r-1) ref=1;
            }
            assert(active==ref);count+=active;
        }
        assert(count==expected_rows[mode]);assert(!c3_score_row_active(mode,240));
        for(unsigned mono=0;mono<2;mono++)for(unsigned grid=0;grid<2;grid++)for(unsigned osd=0;osd<2;osd++) {
            c3_score_t s;c3_score_reset(&s);
            for(unsigned f=0;f<6;f++) {
                unsigned saved=s.score,settled=s.settled;
                unsigned sample=c3_score_schedule_frame(&s,mode,0);
                assert(sample==(mode && ((mode==2||mode==4)||!(f&1))));
                for(unsigned y=0;y<240;y++) {
                    uint8_t a[640],b[640],bits[40];
                    row(y);
                    for(unsigned i=0;i<40;i++)bits[i]=i%3==0?0:(uint8_t)(random32()>>24);
                    const uint8_t *mask=osd&&(y<24||y>=216)?bits:NULL;
                    oracle_overlay(b,y,mono,grid,c3_score_is_center(mode),mask);
                    if(sample&&c3_score_row_active(mode,y)) {
                        memcpy(a,raw,640);c3_score_sample_row(&s,y,a);assert(!memcmp(a,raw,640));
                        c3_effect_score_line(a,y,mono,grid,0,mask);
                        if(c3_score_is_center(mode))c3_effect_score_corners(a,y);
                    } else fast_overlay(a,y,mono,grid,c3_score_is_center(mode),mask);
                    assert(!memcmp(a,b,640));++rows;
                }
                if(sample) {assert(s.count==expected_samples[mode]);c3_score_end_frame(&s);assert(s.settled);}
                else {assert(!s.sum&&!s.count);c3_score_end_frame(&s);assert(s.score==saved&&s.settled==settled);}
                ++frames;
            }
        }
    }
    /* Mode changes always sample the first frame, and shared CENTER never
     * skips even when the previous independent frame armed a skip. */
    c3_score_t s;c3_score_reset(&s);
    for(unsigned i=0;i<30;i++) {
        unsigned mode=i%5;
        assert(c3_score_schedule_frame(&s,mode,0)==(mode!=0));
        if(mode) {s.sum=expected_samples[mode]*7;s.count=expected_samples[mode];c3_score_end_frame(&s);assert(s.score==28);}
    }
    assert(c3_score_schedule_frame(&s,C3_SCORE_CENTER,0));
    c3_score_end_shared_center(&s,19200*8u);assert(s.score==32);
    for(unsigned f=0;f<8;f++) {assert(c3_score_schedule_frame(&s,C3_SCORE_CENTER,1));c3_score_end_shared_center(&s,19200*8u);assert(s.score==32);}
    assert(c3_score_schedule_frame(&s,C3_SCORE_CENTER,0));
    c3_score_end_shared_center(&s,19200*8u);assert(!c3_score_schedule_frame(&s,C3_SCORE_CENTER,0));
    c3_score_reset(&s);assert(c3_score_schedule_frame(&s,C3_SCORE_CENTER,0));
    printf("PASS: all 65536 RGB565 LUT values; %u scheduled frames / %u rows; sparse active rows 60/120; exact fast/score overlay, held score, mode/reset/shared cadence\n",frames,rows);
}

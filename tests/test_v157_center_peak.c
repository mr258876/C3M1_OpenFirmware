#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c3_preview_peak_center.h"

static uint32_t seed = 0x15303B9Du;
static uint32_t rnd(void) {
    seed ^= seed<<13u; seed ^= seed>>17u; seed ^= seed<<5u;
    return seed;
}
static void failure(const char *s,unsigned cfg,unsigned iter,unsigned y,unsigned x){
    fprintf(stderr,"FAIL %s cfg=%u run=%u y=%u x=%u\n",s,cfg,iter,y,x);
    exit(1);
}

static uint16_t as565(const uint8_t *row, uint16_t x) {
    return c3_effect_get565(row,x);
}
/* Outside ROI is not allowed to peak; all other overlays must be unchanged. */
static uint16_t expected_outside(uint16_t orig,uint16_t x,uint16_t y,
             uint8_t mono,uint8_t grid,uint8_t score,const uint8_t *mask) {
    uint16_t c=mono?c3_effect_gray565(orig):orig;
    if (grid && (y==80u || y==160u || x==106u || x==213u))
        c=c3_effect_grid565(c);
    if (score && c3_focus_roi_corner(x,y)) c=0x07E0u;
    if(mask) {
        c=c3_effect_dim565(c);
        if(mask[x>>3u] & (uint8_t)(0x80u>>(x&7u))) c=0xFFFFu;
    }
    return c;
}

int main(void) {
    unsigned long long pixels=0;
    for (unsigned cfg=0;cfg<8u;++cfg) {
        uint8_t grid=(uint8_t)(cfg&1u);
        uint8_t mono=(uint8_t)((cfg>>1u)&1u);
        uint8_t score=(uint8_t)((cfg>>2u)&1u);
        for(unsigned rep=0;rep<14u;++rep) {
            uint16_t color=(uint16_t)rnd();
            uint8_t threshold=(uint8_t)(rep==0 ? 0 : rep==1 ? 24 : rep==2 ? 63 : rnd()%127u);
            c3_focus_t old,center;
            c3_focus_reset(&old);
            for(unsigned i=0;i<C3_FOCUS_W;++i)old.prev_row[i]=(uint8_t)rnd();
            old.score=(uint8_t)(rnd()%100u);
            old.threshold=(uint8_t)(rnd()%55u);
            old.settled=(uint8_t)(rnd()&1u);
            center=old;
            c3_focus_begin_frame(&old);
            c3_focus_begin_frame(&center);
            for(uint16_t y=0;y<240u;++y) {
                struct guarded {uint8_t head[16], row[640], tail[16];} a,b;
                memset(&a,0xA6,sizeof(a)); memset(&b,0xA6,sizeof(b));
                uint8_t orig[640];
                for(unsigned i=0;i<640u;++i)
                    orig[i]=a.row[i]=b.row[i]=(uint8_t)rnd();
                uint8_t mask[40];
                for(unsigned i=0;i<40u;++i)mask[i]=(uint8_t)rnd();
                const uint8_t *pmask=((y<24u || y>=216u) && ((rep&1u)==0u)) ? mask:0;
                c3_effect_peak_line_staged(a.row,y,&old,threshold,color,mono,grid,score,pmask);
                c3_effect_peak_line_center(b.row,y,&center,threshold,color,mono,grid,score,pmask);
                for(uint16_t x=0;x<320u;++x) {
                    uint16_t value=as565(b.row,x);
                    if(y>=C3_FOCUS_ROI_Y0 && y<C3_FOCUS_ROI_Y1 &&
                       x>=C3_FOCUS_ROI_X0 && x<C3_FOCUS_ROI_X1) {
                        if(value!=as565(a.row,x))failure("ROI pixel differs from D3",cfg,rep,y,x);
                    } else {
                        uint16_t exp=expected_outside(as565(orig,x),x,y,mono,grid,score,pmask);
                        if(value!=exp)failure("outside ROI overlay",cfg,rep,y,x);
                    }
                    ++pixels;
                }
                if(old.roi_sum!=center.roi_sum)
                    failure("ROI gradient score sum",cfg,rep,y,0);
                if(y>=59u && y<180u)
                    for(unsigned x=80u;x<240u;++x)
                        if(old.prev_row[x]!=center.prev_row[x])
                            failure("previous-row luma",cfg,rep,y,x);
                for(unsigned i=0;i<16u;++i)
                    if(a.head[i]!=0xA6u || a.tail[i]!=0xA6u ||
                       b.head[i]!=0xA6u || b.tail[i]!=0xA6u)
                        failure("buffer overwrite",cfg,rep,y,i);
            }
            c3_focus_end_frame(&old);
            c3_focus_end_frame(&center);
            if(old.score!=center.score || old.threshold!=center.threshold ||
               old.settled!=center.settled || old.roi_sum!=center.roi_sum)
                failure("final focus score/adaptive threshold",cfg,rep,240u,0);
        }
    }
    printf("PASS: %llu pixels; ROI pixel-exact vs D3, score exact, outside ROI no peak, 8 configurations x 14 frames\n",pixels);
    return 0;
}

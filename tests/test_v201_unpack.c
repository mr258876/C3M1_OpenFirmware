#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_dma_unpack.h"
#include "c3_dma_green.h"
static uint32_t seed=0x20201197u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint16_t gpio(uint8_t v){return (uint16_t)(((uint16_t)v<<1)|(rnd()&0xfe01u));}
static void encode(uint16_t *src,const uint8_t *raw){for(unsigned i=0;i<640;i++)src[i]=gpio(raw[i]);}
static void check_guard(const uint8_t *p,unsigned size){for(unsigned i=0;i<size;i++)assert(p[i]==0xA5);}
int main(void){
    _Alignas(4) uint8_t raw[640],buf[672],green[352];
    _Alignas(4) uint16_t source[640],copy[640];
    uint8_t *out=buf+16,*g=green+16;
    /* Exhaust every RGB565 value, with unrelated GPIO bits randomized. */
    for(unsigned base=0;base<65536;base+=320){
        for(unsigned x=0;x<320;x++){uint16_t v=(uint16_t)(base+x);raw[2*x]=v>>8;raw[2*x+1]=v;}
        encode(source,raw);memcpy(copy,source,sizeof(source));
        memset(buf,0xA5,sizeof(buf));memset(green,0xA5,sizeof(green));
        c3_unpack_pack32(out,source);assert(!memcmp(out,raw,640));check_guard(buf,16);check_guard(out+640,16);
        c3_unpack_asm(out,source);assert(!memcmp(out,raw,640));
        c3_unpack_fused(out,source,g);assert(!memcmp(out,raw,640));
        for(unsigned x=0;x<320;x++)assert(g[x]==c3_green_luma(raw+2*x));
        check_guard(buf,16);check_guard(out+640,16);check_guard(green,16);check_guard(g+320,16);
        assert(!memcmp(copy,source,sizeof(source)));
    }
    unsigned frames=0,lines=0,cached_rows=0;
    for(unsigned peak=0;peak<5;peak++)for(unsigned score=0;score<5;score++)for(unsigned cfg=0;cfg<8;cfg++){
        c3_focus_t f1,f2;c3_score_t s1,s2;c3_peak_exp_t e1={0},e2={0};
        c3_focus_reset(&f1);c3_focus_reset(&f2);c3_score_reset(&s1);c3_score_reset(&s2);
        for(unsigned k=0;k<5;k++){
            c3_live_render_t p={.grid=(cfg>>1)&1,.mono=cfg&1,.peaking=!!peak,.score_center=c3_score_is_center(score),.score_mode=score,.peak_area=peak?peak-1:0,.threshold=f1.threshold,.color=(uint16_t)(0xf800^(cfg*0x421))};
            p.score_shared=p.peaking&&c3_peak_exp_shared(p.peak_area,p.score_mode);
            p.score_sample=c3_score_schedule_frame(&s1,score,p.score_shared);assert(p.score_sample==c3_score_schedule_frame(&s2,score,p.score_shared));
            c3_peak_exp_begin(&e1,p.peak_area,p.peaking);c3_peak_exp_begin(&e2,p.peak_area,p.peaking);
            if(peak){c3_focus_begin_frame(&f1);c3_focus_begin_frame(&f2);}
            for(unsigned y=0;y<240;y++){
                _Alignas(4) uint8_t ref[640],bits[40];
                for(unsigned x=0;x<320;x++){
                    uint16_t v=(uint16_t)rnd();if(k==0)v=0;if(k==1)v=(x^y)&1?0xffff:0;if(k==2)v=(x/9+y/11)&1?0x7bef:0x39e7;if(k==3)v=(uint16_t)(x*31+y*593);
                    raw[2*x]=v>>8;raw[2*x+1]=v;
                }
                for(unsigned i=0;i<40;i++)bits[i]=i%3?(uint8_t)rnd():0;
                const uint8_t *mask=(cfg&4)&&(y<24||y>=216)?bits:0;
                encode(source,raw);memcpy(ref,raw,640);
                c3_live_render_row(ref,y,mask,&p,&f1,&s1,&e1);
                if(c3_dma_green_needed(&p,y,e2.phase)){
                    c3_unpack_fused(out,source,g);c3_dma_green_render(out,g,y,mask,&p,&f2,&s2,&e2);cached_rows++;
                }else{c3_unpack_pack32(out,source);c3_live_render_row(out,y,mask,&p,&f2,&s2,&e2);}
                if(memcmp(out,ref,640)){fprintf(stderr,"mismatch peak%u score%u cfg%u frame%u row%u\n",peak,score,cfg,k,y);return 1;}
                assert(f1.roi_sum==f2.roi_sum&&!memcmp(f1.prev_row,f2.prev_row,320));
                assert(s1.sum==s2.sum&&s1.count==s2.count&&!memcmp(s1.prev_row,s2.prev_row,320));
                check_guard(buf,16);check_guard(out+640,16);check_guard(green,16);check_guard(g+320,16);lines++;
            }
            if(peak){c3_focus_end_frame_area(&f1,c3_peak_exp_roi_count(p.peak_area));c3_focus_end_frame_area(&f2,c3_peak_exp_roi_count(p.peak_area));}
            if(p.score_shared&&!c3_peak_exp_shared_accum(p.peak_area,p.score_mode)){c3_score_end_shared_center(&s1,f1.roi_sum);c3_score_end_shared_center(&s2,f2.roi_sum);}else if(p.score_sample){c3_score_end_frame(&s1);c3_score_end_frame(&s2);}
            assert(f1.score==f2.score&&f1.threshold==f2.threshold&&s1.score==s2.score&&s1.sum==s2.sum&&s1.count==s2.count);frames++;
        }
    }
    printf("PASS v201: 65536 RGB565 values with random unused GPIO bits; %u frames %u rows exact pixels, score/peak state, guards; %u cached green rows. Host ASM uses C fallback, linked ARM checked separately.\n",frames,lines,cached_rows);
}

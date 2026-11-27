#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c3_focus.h"
#include "c3_preview_peak_staged.h"

static uint32_t seed=0x91523A71u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void fail(const char*what,unsigned cfg,unsigned run,unsigned y,unsigned index)
{ fprintf(stderr,"FAIL %s cfg=%u run=%u row=%u byte=%u\n",what,cfg,run,y,index); exit(1); }

int main(void)
{
    uint64_t pixels=0u;
    for (unsigned cfg=0; cfg<8u; ++cfg) {
        const uint8_t grid=(uint8_t)(cfg&1u);
        const uint8_t mono=(uint8_t)((cfg>>1)&1u);
        const uint8_t score=(uint8_t)((cfg>>2)&1u);
        for (unsigned run=0; run<10u; ++run) {
            c3_focus_t a,b;
            c3_focus_reset(&a);
            for(unsigned i=0; i<C3_FOCUS_W; ++i) a.prev_row[i]=(uint8_t)rnd();
            c3_focus_begin_frame(&a);
            b=a;
            uint8_t peak_threshold=(uint8_t)(rnd()%128u);
            uint16_t peak_color=(uint16_t)rnd();
            for(uint16_t y=0u; y<240u; ++y) {
                struct {uint8_t pre[16]; uint8_t line[640]; uint8_t post[16];} reference, staged;
                memset(&reference,0xA5,sizeof(reference));
                memset(&staged,0xA5,sizeof(staged));
                for(unsigned x=0; x<640u; ++x)
                    reference.line[x]=staged.line[x]=(uint8_t)rnd();
                uint8_t mask[40];
                for(unsigned i=0;i<40;i++)mask[i]=(uint8_t)rnd();
                const uint8_t *pmask=(run%2u==0u && (y<24u || y>=216u))?mask:NULL;
                c3_effect_peak_line(reference.line,y,&a,peak_threshold,
                                    peak_color,mono,grid,score,pmask);
                c3_effect_peak_line_staged(staged.line,y,&b,peak_threshold,
                                           peak_color,mono,grid,score,pmask);
                if(memcmp(reference.line,staged.line,640u)) {
                    for(unsigned x=0;x<640u;x++) if(reference.line[x]!=staged.line[x])
                        fail("RGB565 output",cfg,run,y,x);
                }
                if(memcmp(&a,&b,sizeof(a))) fail("gradient/score state",cfg,run,y,0);
                for(unsigned i=0;i<16u;i++) {
                    if(reference.pre[i]!=0xA5u || reference.post[i]!=0xA5u ||
                       staged.pre[i]!=0xA5u || staged.post[i]!=0xA5u)
                        fail("out-of-bounds store",cfg,run,y,i);
                }
                pixels+=320u;
            }
            c3_focus_end_frame(&a); c3_focus_end_frame(&b);
            if(memcmp(&a,&b,sizeof(a))) fail("final normalized score",cfg,run,240u,0u);
        }
    }
    printf("PASS: %llu pixels; staged peaking pixel+state exact, 8 configs x 10 frames\n",(unsigned long long)pixels);
    return 0;
}

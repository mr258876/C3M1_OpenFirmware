/* v0.1.44: exact match against v0.1.41 reference including odd-aligned
 * test buffers. Actual LCD DMA lines are 4-byte aligned; optimized helper
 * uses builtin memcpy to avoid unsafe uint16_t aliasing/unaligned casts. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_focus.h"
#include "c3_preview_peak_staged.h"
static uint32_t seed = 0xE871BB91u;
static uint32_t rnd(void) {seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;}
int main(void) {
    uint64_t pixels=0;
    for (unsigned cfg=0;cfg<8u;++cfg) for(unsigned misalign=0;misalign<2u;++misalign) {
        c3_focus_t old,new_state;
        c3_focus_reset(&old);
        for(unsigned i=0;i<320;i++) old.prev_row[i]=(uint8_t)rnd();
        c3_focus_begin_frame(&old); new_state=old;
        uint8_t threshold=(uint8_t)(rnd()%127u);
        uint16_t peak_color=(uint16_t)rnd();
        for (uint16_t y=0;y<240u;++y) {
            uint8_t src_store[640+64], dst_store[640+64];
            memset(src_store,0xA5,sizeof(src_store));
            memset(dst_store,0xA5,sizeof(dst_store));
            uint8_t *src=src_store+16+misalign;
            uint8_t *dst=dst_store+16+misalign;
            for(unsigned x=0;x<640u;x++) src[x]=dst[x]=(uint8_t)rnd();
            uint8_t mask[40];for(unsigned i=0;i<40;i++)mask[i]=(uint8_t)rnd();
            const uint8_t *m = (y<24u||y>=216u)?mask:NULL;
            c3_effect_peak_line(src,y,&old,threshold,peak_color,
                                  (uint8_t)((cfg>>1)&1u),(uint8_t)(cfg&1u),
                                  (uint8_t)((cfg>>2)&1u),m);
            c3_effect_peak_line_staged(dst,y,&new_state,threshold,peak_color,
                                  (uint8_t)((cfg>>1)&1u),(uint8_t)(cfg&1u),
                                  (uint8_t)((cfg>>2)&1u),m);
            if(memcmp(src,dst,640u) || memcmp(&old,&new_state,sizeof(old))) {
                fprintf(stderr,"FAIL peak cfg=%u odd=%u y=%u\n",cfg,misalign,y);return 1;
            }
            for(unsigned x=0;x<16u;x++) if(src_store[x]!=0xA5||dst_store[x]!=0xA5 ||
                src_store[16+misalign+640+x]!=0xA5||dst_store[16+misalign+640+x]!=0xA5) {
                fprintf(stderr,"FAIL guard cfg=%u odd=%u y=%u\n",cfg,misalign,y);return 1;
            }
            pixels+=320u;
        }
        c3_focus_end_frame(&old);c3_focus_end_frame(&new_state);
        if(memcmp(&old,&new_state,sizeof(old))) return 2;
    }
    printf("PASS: %llu pixels incl. odd buffer addresses, exact RGB565 and focus state\n",
          (unsigned long long)pixels);
    return 0;
}

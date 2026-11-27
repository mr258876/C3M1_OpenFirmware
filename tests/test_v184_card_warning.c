#include "c3_osd.h"
#include "c3_exposure.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t on[2][C3_OSD_BAND_H][C3_OSD_STRIDE];
static void prep(uint8_t sd)
{
    c3_osd_prepare(123u,sd,1u,0u,0u,1u,0u,0u,0u,1u,4u,3u,4u,2,1u,0u,0u,NULL);
}
static void check_mask(uint8_t visible)
{
    prep(0u);
    unsigned glyphs=0;
    for(unsigned band=0;band<2;band++) for(unsigned y=0;y<24;y++) for(unsigned b=0;b<40;b++) {
        uint8_t expected=on[band][y][b];
        if (band==1 && b>=10 && b<20) {
            if(!visible) expected=0;
            glyphs += c3_osd_mask[band][y][b]!=0;
        }
        assert(c3_osd_mask[band][y][b]==expected);
    }
    assert(visible ? glyphs>0 : glyphs==0);
    /* Actual DMA recoloring has no effect on other pixels, including an
       already red background/peaking pixel, and uses correct RGB565 order. */
    for (unsigned y=221;y<235;y++) {
        uint8_t line[640], expected[640];
        memset(line,0xA5,sizeof(line)); memcpy(expected,line,sizeof(line));
        const uint8_t *mask=c3_osd_row((uint16_t)y);
        for(unsigned x=0;x<320;x++) {
            if(mask[x/8u] & (0x80u>>(x%8u))) {
                line[2*x]=line[2*x+1]=0xFF;
                uint16_t col=c3_osd_glyph_color(visible,x,y);
                expected[2*x]=(uint8_t)(col>>8); expected[2*x+1]=(uint8_t)col;
            }
        }
        c3_osd_color_card_row(line,mask);
        assert(memcmp(line,expected,sizeof(line))==0);
    }
}
int main(void)
{
    uint32_t t=0xFFFFFF00u;
    assert(!c3_osd_update_card_warning(1u,t-1u)); prep(1u);
    assert(c3_osd_update_card_warning(0u,t)); prep(0u);
    memcpy(on,c3_osd_mask,sizeof(on));
    for(unsigned i=0;i<4000u;i++) {
        uint8_t visible=(i%1000u)<500u;
        assert(c3_osd_update_card_warning(0u,t+i)==visible);
        check_mask(visible);
    }
    assert(!c3_osd_update_card_warning(0u,t+4500u)); check_mask(0u);
    assert(!c3_osd_update_card_warning(1u,t+4501u)); prep(1u);
    assert(memcmp(on,c3_osd_mask,sizeof(on))!=0);
    /* A new fault starts ON even if previous fault ended OFF. */
    assert(c3_osd_update_card_warning(0u,t+4502u)); check_mask(1u);
    assert(c3_osd_glyph_color(1u,79u,221u)==0xFFFFu);
    assert(c3_osd_glyph_color(1u,80u,221u)==0xF800u);
    assert(c3_osd_glyph_color(1u,159u,234u)==0xF800u);
    assert(c3_osd_glyph_color(1u,160u,234u)==0xFFFFu);
    assert(c3_osd_glyph_color(1u,80u,220u)==0xFFFFu);
    assert(c3_osd_glyph_color(1u,80u,235u)==0xFFFFu);
    puts("PASS v184: 4000ms, 500/500 blink, tick wrap, recovery, cache, exact red-only glyph pixels");
}

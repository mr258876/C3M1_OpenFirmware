#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define C3_ROW_PACK16 1
static uint8_t fifo[640];
static unsigned pos;
/* Each C3_ROW_FIFO_BYTE call represents exactly one complete PB4-low,
 * PGx-IDR sample, PB4-high triplet. Count and sequence are also checked. */
static unsigned gpio_steps;
static uint8_t fifo_sample(void) {
    assert(pos < sizeof(fifo));
    assert(gpio_steps == pos * 3u);
    gpio_steps++; /* RCLK low */
    uint8_t v = fifo[pos++];
    gpio_steps++; /* GPIO sample */
    gpio_steps++; /* RCLK high */
    return v;
}
static uint16_t dim565(uint16_t c) { return (uint16_t)((c & 0xF7DEu) >> 1); }
#define C3_ROW_FIFO_BYTE() fifo_sample()
#define C3_ROW_DIM565(c) dim565(c)
#include "../C3M1_Camera/Core/Inc/c3_preview_row_opt.h"

static uint32_t seed = 0x39a1b4c7u;
static uint32_t rand32(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
static void reference_osd(uint8_t *out,const uint8_t *mask) {
    for(unsigned x=0; x<320; x++) {
        uint16_t raw = ((uint16_t)fifo[2*x]<<8) | fifo[2*x+1];
        uint16_t color = (mask[x/8] & (0x80u >> (x&7))) ? 0xffff : dim565(raw);
        out[2*x]=(uint8_t)(color>>8);
        out[2*x+1]=(uint8_t)color;
    }
}
int main(void) {
    _Alignas(4) uint8_t out[644];
    uint8_t expected[640], mask[40];
    for (unsigned t=0; t<6000; t++) {
        for (unsigned i=0; i<640; i++) fifo[i]=(uint8_t)rand32();
        for (unsigned j=0; j<40; j++)
            mask[j]=(t==0 ? 0u : t==1 ? 0xffu : t==2 ? 0xAAu :
                     t==3 ? 0x55u : t==4 ? (uint8_t)(1u << (j%8)) :
                     t%7==0 ? 0u : (uint8_t)rand32());
        memset(out,0xA7,sizeof(out));
        pos=0; gpio_steps=0;
        c3_row_copy_plain(out);
        assert(pos==640 && gpio_steps==1920 && out[640]==0xA7 && out[641]==0xA7);
        assert(!memcmp(out,fifo,640));
        reference_osd(expected,mask);
        memset(out,0xD4,sizeof(out));
        pos=0; gpio_steps=0;
        c3_row_copy_osd(out,mask);
        assert(pos==640 && gpio_steps==1920 && out[640]==0xD4 && out[641]==0xD4);
        assert(!memcmp(out,expected,640));
    }
    puts("v0.1.39 packed SRAM preview: 6000 randomized rows x 2, byte/sequence invariants PASS");
}

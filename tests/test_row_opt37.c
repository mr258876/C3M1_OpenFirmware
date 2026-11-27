#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static uint8_t fifo_in[640];
static unsigned fifo_cursor;
static uint8_t get_byte(void) {
    assert(fifo_cursor < 640);
    return fifo_in[fifo_cursor++];
}
static uint16_t dim565(uint16_t c) { return (uint16_t)((c & 0xF7DEu) >> 1); }
#define C3_ROW_FIFO_BYTE() get_byte()
#define C3_ROW_DIM565(c) dim565(c)
#include "../C3M1_Camera/Core/Inc/c3_preview_row_opt.h"

static uint32_t state=0x47cafe31u;
static uint32_t rng(void) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
static void reference_osd(uint8_t *dst, const uint8_t *mask) {
    fifo_cursor=0;
    for(unsigned x=0;x<320;x++) {
        uint16_t hi=get_byte();
        uint16_t raw=(hi<<8) | get_byte();
        uint16_t out=dim565(raw);
        if(mask[x>>3] & (0x80u >> (x&7))) out=0xFFFFu;
        dst[2*x]=(uint8_t)(out>>8);
        dst[2*x+1]=(uint8_t)out;
    }
    assert(fifo_cursor==640);
}
int main(void) {
    _Alignas(4) uint8_t b0[642], optimized[642], mask[40];
    for(unsigned t=0;t<3000;t++) {
        for(unsigned i=0;i<640;i++) fifo_in[i]=(uint8_t)rng();
        for(unsigned j=0;j<40;j++) {
            if(t==0) mask[j]=0;
            else if(t==1) mask[j]=255;
            else if(t==2) mask[j]=0xAA;
            else if(t==3) mask[j]=0x55;
            else if(t==4) mask[j]=(uint8_t)(1u << (j%8));
            else if(t%5==0) mask[j]=(rng()%8==0) ? (uint8_t)rng():0;
            else mask[j]=(uint8_t)rng();
        }
        memset(optimized, 0xAC, sizeof(optimized));
        memcpy(b0, fifo_in, 640);
        fifo_cursor=0;
        c3_row_copy_plain(optimized);
        assert(fifo_cursor==640 && optimized[640]==0xAC && optimized[641]==0xAC);
        assert(memcmp(b0, optimized, 640)==0);
        reference_osd(b0,mask);
        fifo_cursor=0;
        memset(optimized, 0xBD, sizeof(optimized));
        c3_row_copy_osd(optimized,mask);
        assert(fifo_cursor==640 && optimized[640]==0xBD && optimized[641]==0xBD);
        assert(memcmp(b0,optimized,640)==0);
    }
    puts("v0.1.37 preview row variants: 3000 randomized reference comparisons PASS");
    return 0;
}

/* Execute actual DNG writer, LUT and readback logic on host. The Python
 * companion checks every emitted TIFF tag and all 180 source means. */
#include "c3_dng.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[256];
static unsigned read_count;
static unsigned unstable_gain,unstable_aec,read_failure;
static uint8_t mock(uint8_t reg,uint8_t *out)
{
    if(read_failure) return 0;
    *out=regs[reg];
    if(reg==0x00 && unstable_gain) *out=(uint8_t)(regs[reg]+(read_count++&1));
    if(reg==0x10 && unstable_aec) *out=(uint8_t)(regs[reg]+(read_count++&1));
    return 1;
}
static void emit(FILE *out)
{
    uint8_t hdr[C3_DNG_HEADER_BYTES],tail[C3_DNG_TAIL_BYTES],again[C3_DNG_TAIL_BYTES];
    c3_dng_make_header(hdr);c3_dng_make_tail(tail);c3_dng_make_tail(again);
    assert(memcmp(tail,again,sizeof tail)==0);
    assert(fwrite(hdr,1,sizeof hdr,out)==sizeof hdr);
    assert(fwrite(tail,1,sizeof tail,out)==sizeof tail);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *f=fopen(argv[1],"wb");assert(f);
    regs[0x13]=0xFA;regs[0x02]=100;regs[0x03]=128;regs[0x01]=240;
    assert(c3_dng_capture_preview_awb(mock));
#if C3_DNG_BLACK_CALIBRATION
    static const uint8_t gs[]={0x00,0x10,0x30,0x70,0xF0};
    for(unsigned g=0;g<5;++g) for(int d=8;d>=0;--d) {
        c3_dng_set_black_level((uint8_t)d,gs[g],0); emit(f);
    }
    c3_dng_set_black_level(4,0x08,0);emit(f); /* 1.5x */
    c3_dng_set_black_level(4,0x18,0);emit(f); /* 3x */
    c3_dng_set_black_level(4,0xF8,0);emit(f); /* >16x clamped */
    c3_dng_set_black_level(0xFF,0x30,1);emit(f); /* auto shorter than calibrated */
    c3_dng_set_black_level(0xFF,0x30,10);emit(f); /* 7840 ticks */
    c3_dng_set_black_level(0xFF,0x30,8191);emit(f); /* auto longer */
    c3_dng_set_black_level(0xFF,0x30,0);emit(f); /* unknown rows */
    c3_dng_set_black_level(4,0xFF,0);emit(f); /* unknown gain */
    regs[0x00]=0x70;regs[0x08]=0;regs[0x10]=10;
    c3_dng_capture_black_level(4,0x10,0,mock);emit(f); /* actual wins */
    c3_dng_capture_black_level(0xFF,0x10,1,mock);emit(f); /* auto actual rows */
    unstable_gain=1;read_count=0;unstable_aec=1;
    c3_dng_capture_black_level(0xFF,0x10,10,mock);emit(f); /* command fallback */
    unstable_gain=unstable_aec=0;regs[0x00]=0xFF;regs[0x08]=0xFF;regs[0x10]=0xFF;
    c3_dng_capture_black_level(0xFF,0x30,10,mock);emit(f); /* floating reads */
    read_failure=1;c3_dng_capture_black_level(4,0x10,0,mock);emit(f);
    c3_dng_capture_black_level(4,0xFF,0,mock);emit(f); /* failed auto gain */
    c3_dng_begin_shot();emit(f); /* next shot, no stale WB or black */
#else
    emit(f);
#endif
    assert(fclose(f)==0);
    puts("PASS v179 DNG writer/readback fixtures");
}

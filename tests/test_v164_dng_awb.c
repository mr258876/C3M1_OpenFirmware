/* The production DNG writer and AWB register reader, with a deterministic bus. */
#include "c3_dng.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t order[8] = {0x13,0x02,0x03,0x01,0x02,0x03,0x01,0x13};
static uint8_t values[8];
static unsigned seq;
static uint8_t mock(uint8_t reg,uint8_t *out)
{
    if (seq>=8u || reg!=order[seq]) return 0u;
    *out=values[seq++]; return 1u;
}
static uint32_t rd32(const uint8_t *p, uint32_t off)
{
    return (uint32_t)p[off] | (uint32_t)p[off+1]<<8 |
          (uint32_t)p[off+2]<<16 | (uint32_t)p[off+3]<<24;
}
static uint32_t ifd_offset(const uint8_t *p,uint16_t tag)
{
    unsigned n=p[8] | (p[9]<<8);
    uint16_t oldtag=0;
    for(unsigned i=0;i<n;++i) {
        uint32_t a=10u+12u*i;
        uint16_t found=(uint16_t)(p[a]|p[a+1]<<8);
        assert(i==0u || found>oldtag);
        oldtag=found;
        if(found==tag) return rd32(p,a+8);
    }
    return 0u;
}
static void inspect(uint32_t r,uint32_t g,uint32_t b)
{
    uint8_t header[C3_DNG_HEADER_BYTES];
    c3_dng_make_header(header);
    uint32_t at=ifd_offset(header,50728u);
    assert(at+24u<=C3_DNG_HEADER_BYTES);
    assert(rd32(header,at)==r && rd32(header,at+4)==100000u);
    assert(rd32(header,at+8)==g && rd32(header,at+12)==100000u);
    assert(rd32(header,at+16)==b && rd32(header,at+20)==100000u);
}
int main(void)
{
    c3_dng_begin_shot();
    assert(!c3_dng_wb_sensor_valid());
    uint8_t rtest,gtest,btest;
    assert(!c3_dng_get_preview_awb_gains(&rtest,&gtest,&btest));
    inspect(104500u,100000u,53300u);
    uint32_t ratios[3];
    assert(c3_dng_neutral_from_awb(100u,128u,240u,ratios));
    /* After mirroring the green/magenta common mode, r/b remains 2.4. */
    assert(ratios[0] >= 104400u && ratios[0] <= 104500u);
    assert(ratios[1]==100000u);
    assert(ratios[2] >= 43500u && ratios[2] <= 43550u);
    assert(!c3_dng_neutral_from_awb(255u,128u,240u,ratios));
    assert(!c3_dng_neutral_from_awb(0u,128u,240u,ratios));
    assert(!c3_dng_neutral_from_awb(128u,128u,128u,ratios));
    assert(!c3_dng_neutral_from_awb(64u,254u,64u,ratios));
    values[0]=0xFF; values[1]=100; values[2]=128; values[3]=240;
    values[4]=101; values[5]=129; values[6]=240; values[7]=0xFF;
    seq=0;
    assert(c3_dng_capture_preview_awb(mock));
    assert(seq==8 && c3_dng_wb_sensor_valid());
    uint8_t red,green,blue;
    assert(c3_dng_get_preview_awb_gains(&red,&green,&blue));
    assert(red==100u && green==128u && blue==240u);
    /* Rounded average: RED 100, GREEN 128, BLUE 240. */
    inspect(ratios[0],100000u,ratios[2]);
    uint8_t hdr1[512],hdr2[512];
    c3_dng_make_header(hdr1);
    c3_dng_make_header(hdr2);
    assert(memcmp(hdr1,hdr2,512)==0); /* save retry metadata frozen */
    /* An unreliable bus sample must restore fixed 5250K WB. */
    values[4]=0xFF; seq=0;
    assert(!c3_dng_capture_preview_awb(mock));
    assert(!c3_dng_wb_sensor_valid());
    assert(!c3_dng_get_preview_awb_gains(&red,&green,&blue));
    inspect(104500u,100000u,53300u);
    values[4]=101; values[0]=0xF8; values[7]=0xF8; seq=0;
    assert(!c3_dng_capture_preview_awb(mock));
    inspect(104500u,100000u,53300u);
    puts("PASS DNG per-shot AWB sensor snapshot, fallback, static header, retry");
    return 0;
}

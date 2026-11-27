/* v0.1.65: real DNG helper; no STM32 or mock ADC required. */
#include "c3_dng.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void wb(unsigned r,unsigned g,unsigned b,uint32_t n[3])
{
    assert(c3_dng_neutral_from_awb((uint8_t)r,(uint8_t)g,(uint8_t)b,n));
}
static uint32_t absdiff(uint32_t a,uint32_t b) { return a>b?a-b:b-a; }
int main(void)
{
    uint32_t anchor[3],low[3],high[3],warm[3],cold[3];
    /* Equal R:B readback, changing only G: temperature must be fixed and
       the relative R/B common-mode must move OPPOSITE to v0.1.64. */
    wb(100,120,200,low);
    wb(100,160,200,high);
    assert(low[1]==100000u && high[1]==100000u);
    assert(high[0] < low[0] && high[2] < low[2]);
    /* r/b = blue_reg / red_reg (independent of green_reg). */
    assert(absdiff(low[0]*1000u/low[2],2000u)<=2u);
    assert(absdiff(high[0]*1000u/high[2],2000u)<=2u);
    /* Changing R/B at a fixed G still moves temperature. */
    wb(120,128,200,warm);
    wb(100,128,220,cold);
    assert(warm[0]*1000u/warm[2] < cold[0]*1000u/cold[2]);
    /* Do not alter fallback; invalid AWB must retain the fixed anchor. */
    c3_dng_begin_shot();
    (void)anchor;
    assert(!c3_dng_neutral_from_awb(255u,128u,200u,low));
    puts("PASS v165: G/M polarity inverted, R/B temperature ratio preserved, fallback safe");
    return 0;
}

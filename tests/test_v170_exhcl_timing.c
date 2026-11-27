#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "c3_rgb_timing.h"
#include "c3_exposure.h"

int main(void)
{
    uint32_t q=(576u+C3_RGB_EXHCL)*2u;
    uint32_t v=(784u+C3_RGB_EXHCL)*2u;
    assert(c3_exposure_estimated_row_ns(C3_EXPOSURE_PREVIEW,0u) ==
        (uint32_t)(((uint64_t)q*1000000000ull+6000000u)/12000000u));
    assert(c3_exposure_estimated_row_ns(C3_EXPOSURE_RAW,0u)==65333u);
    assert(c3_exposure_estimated_row_ns(C3_EXPOSURE_BMP,0u)==
        c3_exposure_estimated_row_ns(C3_EXPOSURE_PREVIEW,0u));
    uint16_t r=c3_exposure_rows_for_mode(7u,C3_EXPOSURE_PREVIEW,0u);
    assert(r == (uint16_t)((12000000u + 100u*q/2u)/(100u*q)));
    c3_exposure_set_preview_zoom(1u);
    assert(c3_exposure_estimated_row_ns(C3_EXPOSURE_PREVIEW,0u)==
        (uint32_t)(((uint64_t)v*1000000000ull+6000000u)/12000000u));
    assert(c3_exposure_estimated_row_ns(C3_EXPOSURE_BMP,0u)==
        (uint32_t)(((uint64_t)q*1000000000ull+6000000u)/12000000u));
    uint16_t zo=c3_exposure_rows_for_mode(7u,C3_EXPOSURE_PREVIEW,0u);
    assert(zo == (uint16_t)((12000000u+100u*v/2u)/(100u*v)));
    c3_exposure_set_preview_zoom(0u);
    /* A RAW preview snapshot must convert by actual row periods, not old
     * EXHCL=0x9E hardcoded time. */
    uint16_t raw=c3_exposure_raw_rows(r,0u);
    uint16_t expected=(uint16_t)(((uint64_t)r*q+392u)/784u);
    assert(raw == expected);
    printf("v170 EXHCL=%u QVGA=%lu VGA=%lu AEC=%u RAW=%u PASS\n",
           (unsigned)C3_RGB_EXHCL, (unsigned long)q,(unsigned long)v,r,raw);
    return 0;
}

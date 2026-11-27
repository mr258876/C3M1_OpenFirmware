#include "c3_meter.h"
#include "c3_exposure.h"
#include "c3_rgb_timing.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void expect(uint16_t rows, uint32_t ns, const char *wanted)
{
    char label[12];
    c3_meter_shutter_label(label, rows, ns);
    if (strcmp(label, wanted)) {
        fprintf(stderr, "%u rows x %lu ns => %s != %s\n",
                rows, (unsigned long)ns, label, wanted);
        assert(0);
    }
}
int main(void)
{
    const uint32_t one = c3_exposure_preview_row_ns(0);
    const uint32_t four = c3_exposure_preview_row_ns(1);
    assert(one == (uint32_t)((((uint64_t)576u+C3_RGB_EXHCL)*2u*1000000000ull+6000000ull)/12000000ull));
    assert(four == (uint32_t)((((uint64_t)784u+C3_RGB_EXHCL)*2u*1000000000ull+6000000ull)/12000000ull));
    c3_exposure_set_preview_zoom(0);
    assert(one == c3_exposure_estimated_row_ns(C3_EXPOSURE_PREVIEW,0));
    c3_exposure_set_preview_zoom(1);
    assert(four == c3_exposure_estimated_row_ns(C3_EXPOSURE_PREVIEW,0));
    c3_exposure_set_preview_zoom(0);
    uint16_t r100=c3_exposure_rows_for_mode(7,C3_EXPOSURE_PREVIEW,0);
    uint16_t r50 =c3_exposure_rows_for_mode(8,C3_EXPOSURE_PREVIEW,0);
    expect(r100,one,"1/100");
    expect(r50,one,"1/50");
    expect(1,one, C3_RGB_EXHCL==0x80u ? "1/8523" :
           C3_RGB_EXHCL==0x9Eu ? "1/8174" :
           C3_RGB_EXHCL==0x40u ? "1/9375" : "1/10417");
    expect(0,one,"1/---");
    expect(8192,one,"1/---");
    expect(100,0,"1/---");
    expect(8191,four, C3_RGB_EXHCL==0x9Eu ? "1.3S" : "1.2S");
    printf("PASS v172 exposure OSD, EXHCL=0x%x; QVGA=%lu ns 4X=%lu ns, 1/100=%u rows\n",
           C3_RGB_EXHCL,(unsigned long)one,(unsigned long)four,r100);
    return 0;
}

#include "c3_exposure.h"
#include "c3_dng.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint8_t regs[256];
static unsigned calls[256];
static uint8_t mock_write(uint8_t reg, uint8_t val)
{
    regs[reg]=val;
    ++calls[reg];
    return 0;
}
static uint8_t mock_read(uint8_t reg, uint8_t *out)
{
    switch (reg) {
        case 0x13: *out=0xFF; break; /* Preview AWB enabled */
        case 0x02: *out=0x80; break; /* R */
        case 0x03: *out=0x90; break; /* G */
        case 0x01: *out=0xB0; break; /* B */
        default: return 0;
    }
    return 1;
}
int main(void)
{
    /* The DNG neutral MUST be sampled while RGB preview is still running. */
    assert(c3_dng_capture_preview_awb(mock_read));
    assert(c3_dng_wb_sensor_valid());
    uint8_t r,g,b;
    assert(c3_dng_get_preview_awb_gains(&r,&g,&b));
    assert(r==0x80 && g==0x90 && b==0xB0);
    uint8_t hdr_before[C3_DNG_HEADER_BYTES], hdr_after[C3_DNG_HEADER_BYTES];
    c3_dng_make_header(hdr_before);

    c3_exposure_t x;
    c3_exposure_init(&x,9,0);
    memset(calls,0,sizeof(calls));
    /* AUTO AEC/AGC in RAW is independent of AWB. */
    assert(c3_exposure_apply(&x,C3_EXPOSURE_RAW,mock_write));
#if C3_RAW_FREEZE_AWB
    assert((regs[0x13] & 0x02u)==0u);
#else
    assert((regs[0x13] & 0x02u)!=0u);
#endif
    assert((regs[0x13] & 0x05u)==0x05u); /* AEC/AGC auto remain on in fallback */

    c3_exposure_snapshot_t shot={.preview_rows=100,.gain_register=0x20,.valid=1};
    assert(c3_exposure_apply_raw_locked(&shot,0,mock_write));
#if C3_RAW_FREEZE_AWB
    assert(regs[0x13]==0u);
#else
    assert(regs[0x13]==0x02u);
#endif
    c3_dng_make_header(hdr_after);
    assert(memcmp(hdr_before,hdr_after,sizeof(hdr_before))==0);
    assert(c3_dng_get_preview_awb_gains(&r,&g,&b));
    assert(r==0x80 && g==0x90 && b==0xB0);

    c3_exposure_sync_dial(&x,7,10); c3_exposure_gain_next(&x); c3_exposure_gain_next(&x);
    assert(c3_exposure_apply(&x,C3_EXPOSURE_RAW,mock_write));
#if C3_RAW_FREEZE_AWB
    assert((regs[0x13] & 0x07u)==0u);
#else
    assert((regs[0x13] & 0x07u)==0x02u);
#endif
    assert(c3_exposure_apply(&x,C3_EXPOSURE_PREVIEW,mock_write));
    assert((regs[0x13] & 0x02u)!=0u); /* RGB preview restores sensor AWB */
    puts("PASS v172: preview-only AWB capture; RAW freezes AWB; exposure AEC/AGC independent; metadata retains AWB");
    return 0;
}

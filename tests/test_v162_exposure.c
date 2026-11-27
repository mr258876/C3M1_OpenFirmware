#include "c3_exposure.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static unsigned nread, nwrite;
static uint8_t sample_hi=1u, sample_lo=128u, sample_gain=0x30u;
static uint8_t bad_bus, change_hi_once, fail_write_at=99;
static struct {uint8_t r,v;} writes[16];

static uint8_t read_reg(uint8_t reg,uint8_t *value)
{
    ++nread;
    if (bad_bus) { *value=0xFFu; return 1u; }
    if (reg==0x08u) {
        *value=(change_hi_once && nread==1u)?(uint8_t)(sample_hi+1u):sample_hi;
    } else if (reg==0x10u) *value=sample_lo;
    else if (reg==0x00u) *value=sample_gain;
    else assert(0 && "unexpected SCCB register");
    return 1u;
}
static uint8_t write_reg(uint8_t reg,uint8_t value)
{
    assert(nwrite<16);
    writes[nwrite].r=reg;
    writes[nwrite].v=value;
    return nwrite++==fail_write_at;
}
static void expect(unsigned n,uint8_t reg,uint8_t value)
{
    assert(n < nwrite && writes[n].r==reg && writes[n].v==value);
}
int main(void)
{
    c3_exposure_snapshot_t s={0};
    assert(c3_exposure_snapshot_read(&s,read_reg));
    assert(s.valid && s.preview_rows==384u && s.gain_register==0x30u);
    assert(nread==4u);
#if C3_SHUTTER_DATASHEET_TIMING
    assert(c3_exposure_raw_rows(384u,1u)==360u);
    assert(c3_exposure_raw_rows(384u,0u)==719u);
#else
    assert(c3_exposure_raw_rows(384u,1u)==192u);
    assert(c3_exposure_raw_rows(384u,0u)==384u);
#endif
    assert(c3_exposure_raw_rows(1u,1u)==1u);
    assert(c3_exposure_raw_rows(8191u,0u)==8191u);

    nwrite=0u;
    assert(c3_exposure_apply_raw_locked(&s,1u,write_reg));
    assert(nwrite==5u);
    #if C3_RAW_FREEZE_AWB
    expect(0u,0x13u,0x00u); /* AEC/AGC/AWB frozen before RAW */
#else
    expect(0u,0x13u,0x02u); /* historical AWB enabled */
#endif
    expect(1u,0x1Fu,0x00u);
#if C3_SHUTTER_DATASHEET_TIMING
    expect(2u,0x08u,1u);
    expect(3u,0x10u,104u); /* 360 rows */
#else
    expect(2u,0x08u,0u);
    expect(3u,0x10u,192u);
#endif
    expect(4u,0x00u,0x30u);
    nwrite=0u;
    assert(c3_exposure_apply_raw_locked(&s,0u,write_reg));
#if C3_SHUTTER_DATASHEET_TIMING
    expect(2u,0x08u,2u);
    expect(3u,0x10u,207u); /* 719 rows */
#else
    expect(2u,0x08u,1u);
    expect(3u,0x10u,128u);
#endif

    /* A crashed SCCB read is NOT a believable 0xFFFF shutter setting. */
    bad_bus=1u;nread=0u;
    assert(!c3_exposure_snapshot_read(&s,read_reg));
    assert(!s.valid);
    nwrite=0u;
    assert(!c3_exposure_apply_raw_locked(&s,1u,write_reg));
    assert(nwrite==0u);
    bad_bus=0u;

    /* If the upper AEC byte changes during the read, retry once. */
    change_hi_once=1u;nread=0u;
    assert(c3_exposure_snapshot_read(&s,read_reg));
    assert(nread==7u && s.preview_rows==384u);
    change_hi_once=0u;
    fail_write_at=2u;nwrite=0u;
    assert(!c3_exposure_apply_raw_locked(&s,1u,write_reg));
    assert(nwrite==3u);
    nwrite=0u;fail_write_at=99u;
    assert(!c3_exposure_apply_raw_locked(&s,2u,write_reg));
    assert(!c3_exposure_snapshot_read(NULL,read_reg));
    assert(!c3_exposure_snapshot_read(&s,NULL));
    printf("PASS v162: live AEC/GAIN snapshot, SCCB FF rejection, clock row conversion, lock order/failures\n");
    return 0;
}

#include "c3_exposure.h"
#include "c3_shutter_timing.h"
#include "c3_meter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if !C3_SHUTTER_DYNAMIC_TIMING
#error "v177 test requires dynamic exposure enabled"
#endif
static const uint16_t den[9]={8000,4000,2000,1000,500,300,150,100,50};
static const uint16_t x1[9]={174,174,174,174,174,138,138,138,138};
static const uint16_t r1[9]={1,2,4,8,16,28,56,84,168};
static const uint16_t x4[9]={716,716,216,216,216,216,216,216,216};
static const uint16_t r4[9]={1,1,3,6,12,20,40,60,120};
static const uint16_t xr[9]={716,216,216,216,216,216,216,216,216};
static const uint16_t rr[9]={1,3,6,12,24,40,80,120,240};
static void plans(void)
{
    c3_shutter_plan_t p;
    for (unsigned fmt=0;fmt<3u;++fmt) {
        for (unsigned dial=0;dial<9u;++dial) {
            assert(c3_shutter_plan((c3_timing_format_t)fmt,(uint8_t)dial,1u,&p));
            uint16_t wanted_x=(fmt==0?x1[dial]:fmt==1?x4[dial]:xr[dial]);
#if !C3_4X_FAST_8000
            if (fmt==1 && dial==0) wanted_x=128u;
#endif
            uint16_t wanted_r=(fmt==0?r1[dial]:fmt==1?r4[dial]:rr[dial]);
            assert(p.dummy==wanted_x && p.rows==wanted_r);
            assert(p.clkrc==((fmt==1 && dial==0 && C3_4X_FAST_8000)?0x40u:0u));
            assert(c3_shutter_row_ns((c3_timing_format_t)fmt,&p)>0u);
            uint32_t exp_ns=c3_shutter_exposure_ns((c3_timing_format_t)fmt,&p);
            assert(exp_ns);
            uint32_t ideal_ns=(1000000000u+den[dial]/2u)/den[dial];
#if !C3_4X_FAST_8000
            if (fmt==1 && dial==0) assert(exp_ns==152000u);
            else
#endif
            {
                uint32_t delta=exp_ns>ideal_ns?exp_ns-ideal_ns:ideal_ns-exp_ns;
                /* <=8ns rounding except 1X 1/300..1/50 at -0.04%. */
                assert(delta<=8000u);
            }
            if (fmt==0) {
                c3_exposure_set_preview_zoom(0);
                assert(c3_exposure_rows((uint8_t)dial)==wanted_r);
                assert(c3_exposure_preview_row_ns_dial(0,(uint8_t)dial)==c3_shutter_row_ns((c3_timing_format_t)fmt,&p));
                assert(c3_exposure_rows_for_mode((uint8_t)dial,C3_EXPOSURE_BMP,0)==r1[dial]);
            } else if (fmt==1) {
                c3_exposure_set_preview_zoom(1);
                assert(c3_exposure_rows((uint8_t)dial)==wanted_r);
                assert(c3_exposure_preview_row_ns_dial(1,(uint8_t)dial)==c3_shutter_row_ns((c3_timing_format_t)fmt,&p));
                /* 4x preview; saved BMP must be 1x full-field. */
                assert(c3_exposure_rows_for_mode((uint8_t)dial,C3_EXPOSURE_BMP,0)==r1[dial]);
            } else {
                assert(c3_exposure_rows_for_mode((uint8_t)dial,C3_EXPOSURE_RAW,0)==wanted_r);
            }
        }
        assert(c3_shutter_plan((c3_timing_format_t)fmt,0u,0u,&p));
        assert(p.dummy==(fmt==2?0u:128u) && p.clkrc==0u);
        assert(!c3_shutter_plan((c3_timing_format_t)fmt,9u,1u,&p));
    }
    c3_exposure_set_preview_zoom(0);
    assert(c3_exposure_preview_row_ns_dial(0,0xFFu)==117333u);
    assert(c3_exposure_preview_row_ns_dial(1,0xFFu)==152000u);
    assert(!c3_shutter_plan((c3_timing_format_t)3,0,1,&p));
    puts("PASS v177 27 shutter plans, 1x/4x/RAW AEC/EXHCH/CLKRC, auto unaffected");
}
static uint8_t regs[256];
static uint8_t should_fail_read;
static uint8_t corrupt_after_write;
static unsigned writes,reads,order[16];
static uint8_t wr(uint8_t reg,uint8_t val)
{
    if (writes<16u) order[writes]=reg;
    ++writes;
    regs[reg]=val;
    return 0u;
}
static uint8_t rd(uint8_t reg,uint8_t *val)
{
    ++reads;
    if (should_fail_read || !val) return 0u;
    *val=regs[reg];
    if (corrupt_after_write && writes>=5u && reg==0x2Bu) *val^=1u;
    return 1u;
}
static void reset(void)
{
    memset(regs,0,sizeof(regs));writes=reads=0;
    should_fail_read=corrupt_after_write=0;
}
static c3_exposure_t init_mode(uint8_t dial,c3_shoot_mode_t mode)
{
    c3_exposure_t s;
    c3_exposure_init(&s,mode==C3_SHOOT_P?9u:dial,0u);
    if(mode==C3_SHOOT_M) c3_exposure_gain_next(&s);
    return s;
}
static void program(void)
{
    c3_exposure_t s=init_mode(0,C3_SHOOT_M);
    c3_exposure_set_preview_zoom(1);
    reset();
    /* Start with a different dummy high nibble from the previous RAW mode. */
    regs[0x2Au]=0x10u;
    assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,rd,wr));
#if C3_4X_FAST_8000
    assert(regs[0x11u]==0x40u);
    assert(regs[0x2Au]==0x20u && regs[0x2Bu]==0xCCu); // 716=0x2CC
#else
    assert(regs[0x11u]==0u && regs[0x2Au]==0u && regs[0x2Bu]==0x80u);
#endif
    assert(writes==10u && reads>=9u);
    assert(order[0]==0x13u && order[1]==0x0Eu && order[2]==0x2Eu &&
           order[3]==0x2Du && order[4]==0x34u && order[5]==0x33u &&
           order[6]==0x1Fu && order[7]==0x2Au && order[8]==0x2Bu &&
           order[9]==0x11u);
    assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,wr));
    assert(regs[0x08u]==0u && regs[0x10u]==1u);
    /* FAST preview -> RAW reset: restore CLKRC to 0 and select RAW-only dummy.
     * Actual RAW table init is done by the caller; this checks final timing. */
    writes=0;
    assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_RAW,rd,wr));
    assert(regs[0x11u]==0u && regs[0x2Au]==0x20u && regs[0x2Bu]==0xCCu);
    assert(c3_exposure_apply(&s,C3_EXPOSURE_RAW,wr));
    /* Manually selected 1/8000 should be 1 row in RAW, not inherited rows. */
    assert(regs[0x10u]==1u);
    c3_exposure_snapshot_t shot={.valid=1,.gain_register=0x30u,.preview_rows=419u};
    assert(c3_exposure_apply_raw_locked_timed(&shot,&s,0u,wr));
    assert(regs[0x08u]==0u && regs[0x10u]==1u && regs[0x00u]==0x30u);
    assert(regs[0x13u]==0u);
    /* RAW -> RGB 1x: no accidental remaining 0x2CC high dummy nibble. */
    c3_exposure_set_preview_zoom(0);
    s=init_mode(8,C3_SHOOT_M);
    assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,rd,wr));
    assert(regs[0x2Au]==0u && regs[0x2Bu]==0x8Au && regs[0x11u]==0u);
    assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,wr));
    assert(regs[0x10u]==168u);
    /* Return to auto must reset both EXHCH and EXHCL to the baseline. */
    s=init_mode(7,C3_SHOOT_P);
    assert(c3_exposure_prepare_timing(&s,C3_EXPOSURE_PREVIEW,rd,wr));
    assert(regs[0x2Au]==0u && regs[0x2Bu]==0x80u && regs[0x11u]==0u);
    assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,wr));
    assert(regs[0x13u]&1u);
    /* P RAW inherits exposure time from 1x preview snapshot (not dial rows). */
    shot.preview_rows=80u;
    assert(c3_exposure_apply_raw_locked_timed(&shot,&s,0u,wr));
    assert(regs[0x08u]*256u+regs[0x10u]==c3_exposure_raw_rows(80u,0u));
    /* Fail-closed readback cases. */
    reset();s=init_mode(1,C3_SHOOT_M);
    regs[0x2Au]=0xFFu;
    assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_RAW,rd,wr));
    assert(writes==0u);
    regs[0x2Au]=0x01u;
    assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_RAW,rd,wr));
    assert(writes==0u);
    regs[0x2Au]=0u;should_fail_read=1u;
    assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_RAW,rd,wr));
    assert(writes==0u);
    should_fail_read=0u;corrupt_after_write=1u;
    assert(!c3_exposure_prepare_timing(&s,C3_EXPOSURE_RAW,rd,wr));
    assert(writes==10u);
    puts("PASS v177 hardware-register simulation: fast4x->RAW->RGB, lock, auto, SCCB failure");
}
int main(void){plans();program();return 0;}

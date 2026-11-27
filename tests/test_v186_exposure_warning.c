#include "c3_meter.h"
#include "c3_exposure.h"
#include "c3_osd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t yave, fine, fail_address=0xAAu, h,l,g;
static unsigned yreads, freads;
static uint8_t com8=0xFE, com9=0x31, reglo=48, reghi=64;
static uint8_t reg_read(uint8_t a,uint8_t *v) {
    if(a==fail_address) return 0u;
    switch(a) {
        case 0x2F: *v=yave; ++yreads; break;
        case 0x1F: *v=fine; ++freads; break;
        case 0x08: *v=h; break;
        case 0x10: *v=l; break;
        case 0x00: *v=g; break;
        case 0x13: *v=com8; break;
        case 0x14: *v=com9; break;
        case 0x24: *v=reghi; break;
        case 0x25: *v=reglo; break;
        default: return 0u;
    }
    return 1u;
}
static void sample(c3_meter_t *m,unsigned mode,unsigned rows,unsigned gain,unsigned y,unsigned laec,int ev) {
    m->valid=1u; m->exposure_rows=(uint16_t)rows; m->gain_x16=(uint16_t)gain;
    yave=(uint8_t)y;fine=(uint8_t)laec;
    uint8_t lo,hi;c3_exposure_ev_targets((int8_t)ev,&hi,&lo,0);
    com8=mode==0?0xFF:0xFE;reglo=lo;reghi=hi;
    c3_meter_update_warning(m,(uint8_t)mode,lo,hi,reg_read);
}
static unsigned checks;
static void wait4(c3_meter_t *m,unsigned mode,unsigned rows,unsigned gain,unsigned y,unsigned fine_value,int ev,unsigned expected) {
    c3_meter_reset(m);
    for(unsigned i=0;i<4;i++) {
        sample(m,mode,rows,gain,y,fine_value,ev);
        assert(m->warning==(i==3?expected:0u)); ++checks;
    }
}
static void prep(unsigned mode,c3_meter_t *m,unsigned sd,uint32_t t) {
    c3_osd_update_card_warning((uint8_t)sd,t);
    c3_osd_update_exposure_warning(m,(uint8_t)mode,t);
    c3_osd_prepare(123,sd,1,0,1,1,72,1,294,mode,
        mode==0?C3_EXPOSURE_AUTO_CODE:4,mode==2?3:0,mode==0?9:4,0,mode!=2,0,0,m);
}
static unsigned pixel(unsigned x,unsigned y) {return !!(c3_osd_mask[1][y][x>>3]&(0x80u>>(x&7u)));}
static void blink(unsigned mode) {
    c3_meter_t m={.valid=1,.attempted=1,.exposure_rows=80,.gain_x16=256};
    uint32_t t=0xFFFFFF00u;
    m.last_attempt_ms=t;prep(mode,&m,1,t-1); m.warning=C3_METER_WARN_DARK;
    prep(mode,&m,1,t);
    uint8_t ref[sizeof(c3_osd_mask)];memcpy(ref,c3_osd_mask,sizeof ref);
    for(unsigned i=0;i<3500;i++) {
        if(i%350u==0) m.last_attempt_ms=t+i;
        prep(mode,&m,1,t+i);
        const uint8_t (*on)[24][40]=(const uint8_t (*)[24][40])ref;
        assert(memcmp(on[0],c3_osd_mask[0],sizeof on[0])==0);
        for(unsigned y=0;y<24;y++)for(unsigned x=0;x<320;x++) {
            unsigned expected=!!(on[1][y][x>>3]&(0x80u>>(x&7u)));
            if(i%1000>=500 && ((mode==0 && x<67)||(mode<2 && x>=240 && x<307))) expected=0;
            assert(pixel(x,y)==expected);
        }
    }
    /* Clear a live warning immediately, including the hidden phase. */
    m.last_attempt_ms=t+3501; prep(mode,&m,1,t+3501);
    m.warning=0;prep(mode,&m,1,t+3502);assert(memcmp(ref,c3_osd_mask,sizeof ref)==0);
    /* New fault starts visible, stale samples never flash. */
    m.warning=C3_METER_WARN_BRIGHT;prep(mode,&m,1,t+3503);assert(memcmp(ref,c3_osd_mask,sizeof ref)==0);
    prep(mode,&m,1,t+5500);assert(memcmp(ref,c3_osd_mask,sizeof ref)==0);
    /* Card warning and auto exposure warning can be active at the same time. */
    m.last_attempt_ms=t+6000;prep(mode,&m,0,t+6000);
    m.last_attempt_ms=t+6500;prep(mode,&m,0,t+6500);
    for(unsigned y=0;y<24;y++)for(unsigned x=80;x<160;x++)assert(!pixel(x,y));
    for(unsigned y=0;y<24;y++)for(unsigned x=160;x<240;x++)
        assert(pixel(x,y)==!!( ((const uint8_t (*)[24][40])ref)[1][y][x>>3]&(0x80u>>(x&7u))));
    printf("PASS mode %u: 3500 blink phases, wrap, stale, SD independence\n",mode);
}
int main(void) {
    c3_meter_t m;
    for(unsigned mode=0;mode<3;mode++)for(int ev=-6;ev<=6;ev++) {
        uint8_t lo,hi;c3_exposure_ev_targets((int8_t)ev,&hi,&lo,0);
        for(unsigned y=0;y<256;y++) {
            wait4(&m,mode,1000,256,y,0,ev,mode<2&&y+4<lo?C3_METER_WARN_DARK:0);
            wait4(&m,mode,1,16,y,0,ev,mode<2&&y>hi+4u?C3_METER_WARN_BRIGHT:0);
            wait4(&m,mode,100,128,y,0,ev,0);
        }
    }
    c3_meter_reset(&m);
    for(unsigned i=0;i<20;i++) { sample(&m,0,1000+i*10,256,0,0,0);assert(!m.warning); }
    for(unsigned i=0;i<4;i++)sample(&m,0,1200+i%2,256,0,0,0);
    assert(m.warning==C3_METER_WARN_DARK);
    sample(&m,0,1230,256,0,0,0);assert(!m.warning);
    for(unsigned i=0;i<20;i++) {sample(&m,0,0,16,255,i,0);assert(!m.warning);}
    for(unsigned i=0;i<4;i++)sample(&m,0,0,16,255,19,0);
    assert(m.warning==C3_METER_WARN_BRIGHT);
    sample(&m,0,0,16,50,19,0);assert(!m.warning);
    wait4(&m,0,1,256,0,0,0,0);wait4(&m,0,2,16,255,0,0,0);
    wait4(&m,1,0,256,0,0,0,0);wait4(&m,0,65535,256,0,0,0,0);
    wait4(&m,0,1000,496,0,0,0,C3_METER_WARN_DARK);
    for(unsigned mode=0;mode<2;mode++) {
        for(unsigned raw=0;raw<256;raw++) {
            unsigned gain=c3_meter_decode_gain_x16((uint8_t)raw);
            wait4(&m,mode,1000,gain,0,0,0,gain>=248?C3_METER_WARN_DARK:0);
            wait4(&m,mode,1,gain,255,0,0,gain<=17?C3_METER_WARN_BRIGHT:0);
        }
    }
    /* Distinguish actual incorrect configuration from unrelated init flags. */
    com9=0x11;wait4(&m,1,100,248,0,0,0,0);
    assert(m.warning_reason==C3_METER_REASON_CONFIG);
    com9=0x31;wait4(&m,1,100,248,0,0,0,C3_METER_WARN_DARK);
    reghi=63;c3_meter_update_warning(&m,1,48,64,reg_read);assert(!m.warning && m.warning_reason==C3_METER_REASON_CONFIG);
    reghi=64;com8=0xFA;c3_meter_update_warning(&m,1,48,64,reg_read);assert(!m.warning && m.warning_reason==C3_METER_REASON_CONFIG);
    com8=0xFE;fail_address=0x14;sample(&m,1,100,248,0,0,0);assert(!m.warning && m.warning_reason==C3_METER_REASON_CONFIG);fail_address=0xAA;
    c3_meter_reset(&m);
    for(unsigned i=0;i<4;i++)sample(&m,0,1800+i*4,248,0,0,0);
    assert(m.warning==C3_METER_WARN_DARK);
    wait4(&m,1,100,256,0,0,0,C3_METER_WARN_DARK);
    fail_address=0x2F;sample(&m,1,100,256,0,0,0);assert(!m.warning);fail_address=0xAA;
    wait4(&m,0,0,16,255,0,0,C3_METER_WARN_BRIGHT);
    fail_address=0x1F;sample(&m,0,0,16,255,0,0);assert(!m.warning);fail_address=0xAA;
    wait4(&m,1,100,256,0,0,0,C3_METER_WARN_DARK);
    c3_meter_update_warning(&m,1,48,64,NULL);assert(!m.warning);
    wait4(&m,1,100,256,0,0,0,C3_METER_WARN_DARK);
    m.valid=0;c3_meter_update_warning(&m,1,48,64,reg_read);assert(!m.warning);
    wait4(&m,1,100,256,0,0,0,C3_METER_WARN_DARK);
    c3_meter_reset(&m);assert(!m.warning && !m.warning_samples);
    /* Integration at real meter cadence, no extra YAVE between fresh polls. */
    h=0;l=100;g=0x7F;yave=0;yreads=0;com8=0xFE;com9=0x31;reglo=48;reghi=64;
    for(unsigned t=0;t<1100;t++)if(c3_meter_poll(&m,t,reg_read))c3_meter_update_warning(&m,1,48,64,reg_read);
    assert(yreads==4 && m.warning==C3_METER_WARN_DARK && m.raw_gain==0x7F && m.yave==0 && m.warning_reason==C3_METER_REASON_ACTIVE);
    printf("PASS %u threshold/EV/limit sample checks; P plateau/LAEC, errors, reset, 350ms cadence\n",checks);
    for(unsigned mode=0;mode<3;mode++)blink(mode);
    return 0;
}

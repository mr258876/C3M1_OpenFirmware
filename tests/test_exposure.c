/* Factory dial codes 00..08 and unlabelled 09..0F.
 * Exhaustive software mode x sensor mode x all 16 detents x gain tests.
 */
#include "c3_exposure.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {uint8_t reg,val;} op_t;
static op_t ops[12];
static unsigned count, fail_at;
static uint8_t write_reg(uint8_t a,uint8_t v)
{
    unsigned idx = count++;
    assert(idx < sizeof(ops)/sizeof(ops[0]));
    ops[idx]=(op_t){a,v};
    return (idx==fail_at) ? 1u : 0u;
}
static void reset_log(void) { count=0; fail_at=99u; }
static void expect(uint8_t r,uint8_t v,unsigned i)
{
    assert(i<count && ops[i].reg==r && ops[i].val==v);
}
static void check_gpio_decoder(void)
{
    const uint16_t denoms[9]={8000,4000,2000,1000,500,300,150,100,50};
#if C3_SHUTTER_DATASHEET_TIMING
    const uint16_t rows[9]={1,2,4,8,16,27,54,82,163};
#else
    const uint16_t rows[9]={2,4,8,16,32,53,107,160,320};
#endif
    for (unsigned code=0;code<16;code++) {
        /* PF0,PF1 and PF10 state must not affect shutter code */
        uint16_t port=(uint16_t)((code<<6u)|(1u<<0)|(1u<<1)|(1u<<10));
        assert(c3_exposure_decode_gpio(port)==code);
        assert(c3_exposure_decode_gpio((uint16_t)(code<<6u))==code);
        assert(c3_exposure_dial_defined((uint8_t)code)==(code<9));
        assert(c3_exposure_target_denominator((uint8_t)code)==(code<9?denoms[code]:0));
        assert(c3_exposure_rows((uint8_t)code)==(code<9?rows[code]:0));
    }
    assert(c3_exposure_target_denominator(0xFFu)==0u);
    assert(c3_exposure_rows(C3_EXPOSURE_AUTO_CODE)==0u);
}
static void check_modes(void)
{
    c3_exposure_t s;
    unsigned cases=0;
      for(unsigned sensor=0;sensor<3;sensor++)
        for(unsigned code=0;code<16;code++)
          for(unsigned gain=0;gain<6;gain++) {
            c3_exposure_init(&s,(uint8_t)code,0);
            for(unsigned k=0;k<gain;k++) c3_exposure_gain_next(&s);
            assert(s.shoot_mode==(code>=9?C3_SHOOT_P:gain?C3_SHOOT_M:C3_SHOOT_S));
            uint8_t d=c3_exposure_effective_dial(&s);
            uint8_t g=c3_exposure_effective_gain(&s);
            assert(d==(code<9?code:C3_EXPOSURE_AUTO_CODE));
            assert(g==(code<9?gain:0u));
            reset_log();
            assert(c3_exposure_apply(&s,(c3_exposure_sensor_mode_t)sensor,write_reg));
            uint8_t base=sensor==C3_EXPOSURE_RAW?(C3_RAW_FREEZE_AWB ? 0x00u : 0x02u):0xFAu;
            uint8_t auto_exposure=(d==C3_EXPOSURE_AUTO_CODE);
            expect(0x13u,base,0);
            expect(0x14u,sensor==C3_EXPOSURE_RAW?0x30u:0x31u,1);
            expect(0x0Eu,(!sensor && auto_exposure)?0xF5u:0x00u,2);
            unsigned n=3;
            uint8_t aew,aeb,vpt;
            c3_exposure_ev_targets(0,&aew,&aeb,&vpt);
            expect(0x24u,aew,n++);
            expect(0x25u,aeb,n++);
            expect(0x26u,vpt,n++);
            if(!auto_exposure) {
#if C3_SHUTTER_DATASHEET_TIMING
                uint16_t rows=c3_exposure_rows_for_mode(d,
                                 (c3_exposure_sensor_mode_t)sensor,0u);
#else
                uint16_t rows=c3_exposure_rows(d);
#endif
                expect(0x1Fu,0u,n++); /* clear fine exposure */
                expect(0x08u,(uint8_t)(rows>>8u),n++);
                expect(0x10u,(uint8_t)rows,n++);
            }
            if(g) expect(0x00u,c3_exposure_gain_reg(g),n++);
            expect(0x13u,(uint8_t)(base|(auto_exposure?1u:0u)|(!g?4u:0u)),n++);
            assert(count==n);
            ++cases;
          }
    assert(cases==288u);
}
/* Exhaustive 16x16 physical transitions for each ISO selection. */
static void check_direct_transitions(void)
{
    unsigned cases=0;
    for(unsigned from=0;from<16;from++) for(unsigned to=0;to<16;to++)
      for(unsigned gain=0;gain<6;gain++) {
        c3_exposure_t s;
        c3_exposure_init(&s,from,0xFFFFFFC0u);
        for(unsigned i=0;i<gain;i++) c3_exposure_gain_next(&s);
        unsigned prior=from<9?gain:0;
        assert(s.gain_step==prior);
        assert(!c3_exposure_poll_dial(&s,to,0xFFFFFFE0u));
        assert(!c3_exposure_poll_dial(&s,to,0x1Bu)); /* 59 ms */
        assert(c3_exposure_poll_dial(&s,to,0x1Cu)==(from!=to));
        unsigned expected=to<9?prior:0;
        assert(s.gain_step==expected);
        assert(s.shoot_mode==(to>=9?C3_SHOOT_P:expected?C3_SHOOT_M:C3_SHOOT_S));
        assert(c3_exposure_effective_dial(&s)==(to<9?to:C3_EXPOSURE_AUTO_CODE));
        for(unsigned i=0;i<6;i++) {
            c3_exposure_gain_next(&s);
            unsigned g=to<9?(expected+i+1)%6:0;
            assert(c3_exposure_effective_gain(&s)==g);
            assert(s.shoot_mode==(to>=9?C3_SHOOT_P:g?C3_SHOOT_M:C3_SHOOT_S));
        }
        c3_exposure_sync_dial(&s,9,100u);
        assert(s.gain_step==0 && s.shoot_mode==C3_SHOOT_P);
        c3_exposure_sync_dial(&s,8,101u);
        assert(s.gain_step==0 && s.shoot_mode==C3_SHOOT_S);
        ++cases;
      }
    printf("PASS %u direct dial/ISO transitions with debounce and wrap\n",cases);
    c3_exposure_t s;
    c3_exposure_init(&s,8,0);
    c3_exposure_gain_next(&s);
    assert(!c3_exposure_poll_dial(&s,9,10));
    assert(!c3_exposure_poll_dial(&s,8,40));
    assert(!c3_exposure_poll_dial(&s,8,100));
    assert(s.shoot_mode==C3_SHOOT_M && s.gain_step==1); /* bounce into AUTO must not reset gain */
}
static void check_failures(void)
{
    c3_exposure_t s;
    c3_exposure_init(&s,0u,0u);
    s.shoot_mode=C3_SHOOT_M; s.gain_step=5u;
    reset_log();
    assert(c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
    unsigned n=count;
    for (unsigned i=0;i<n;i++) {
        reset_log();fail_at=i;
        assert(!c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
        assert(count==i+1);
    }
    s.gain_step=6u;reset_log();assert(!c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
    s.gain_step=1u;s.shoot_mode=(c3_shoot_mode_t)3;
    assert(!c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
    s.shoot_mode=C3_SHOOT_M;s.dial_code=9u;
    assert(!c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
}

static void check_ev_compensation(void)
{
    c3_exposure_t s;
    c3_exposure_init(&s,7u,0u);
    assert(s.ev_half_steps==0 && c3_exposure_ev_active(&s));
    uint8_t prev_low=0u,prev_high=0u;
    for (int k=-6;k<=6;k++) {
        uint8_t aew,aeb,vpt;
        c3_exposure_ev_targets((int8_t)k,&aew,&aeb,&vpt);
        assert(aeb<aew && aew<=240 && aeb>0);
        assert((vpt>>4u)*16u > aew); /* fast upper encloses stable upper */
        assert((vpt&15u)*16u < aeb); /* fast lower encloses stable lower */
        if (k!=-6) assert(aew>prev_high && aeb>prev_low);
        prev_high=aew;prev_low=aeb;
        assert(c3_exposure_ev_adjust(&s,k-(int)s.ev_half_steps));
        assert(s.ev_half_steps==k);
        char txt[7];c3_exposure_ev_label(txt,(int8_t)k);
        assert(txt[0]=='E' && txt[1]=='V' && txt[2]==(k<0?'-':'+'));
        assert(txt[3]=='0'+(k<0?-k:k)/2);
        assert(txt[5]=='0'+((k<0?-k:k)&1)*5);
        reset_log(); assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,write_reg));
        expect(0x24,aew,3);expect(0x25,aeb,4);expect(0x26,vpt,5);
        reset_log(); assert(c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
        expect(0x24,aew,3);expect(0x25,aeb,4);expect(0x26,vpt,5);
    }
    assert(!c3_exposure_ev_adjust(&s,1)); /* upper limit */
    assert(s.ev_half_steps==6);
    assert(c3_exposure_ev_adjust(&s,-50));
    assert(s.ev_half_steps==-6);
    assert(!c3_exposure_ev_adjust(&s,-1)); /* lower limit */
    assert(c3_exposure_ev_adjust(&s,6));
    assert(s.ev_half_steps==0);
    uint8_t aew,aeb,vpt;
    c3_exposure_ev_targets(0,&aew,&aeb,&vpt);
    assert(aew==0x40 && aeb==0x30 && vpt==0xA1);
    assert(s.shoot_mode==C3_SHOOT_S && c3_exposure_ev_active(&s));
    assert(c3_exposure_ev_adjust(&s,4));
    reset_log();assert(c3_exposure_apply(&s,C3_EXPOSURE_PREVIEW,write_reg));
    expect(0x24u,156u,3);
    c3_exposure_gain_next(&s);
    assert(s.shoot_mode==C3_SHOOT_M && !c3_exposure_ev_active(&s));
    reset_log();assert(c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
    expect(0x24u,0x40u,3);
    c3_exposure_sync_dial(&s,15,200u);
    assert(s.shoot_mode==C3_SHOOT_P && c3_exposure_ev_active(&s));
    reset_log();assert(c3_exposure_apply(&s,C3_EXPOSURE_RAW,write_reg));
    expect(0x24u,156u,3);
    c3_exposure_sync_dial(&s,0,250u);
    assert(s.shoot_mode==C3_SHOOT_S && c3_exposure_effective_dial(&s)==0u);
    puts("EV: 13 nominal steps, both formats, auto-only, limits, physical dial sync PASS");
}

int main(void)
{
    check_gpio_decoder();check_modes();check_direct_transitions();check_failures();check_ev_compensation();
    puts("EXPOSURE: 288 mode/register profiles + 16 GPIO codes + AUTO detents + SCCB errors PASS");
    return 0;
}

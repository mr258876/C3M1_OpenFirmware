/* Real exposure snapshot logic, simulated register reads and frame barrier.
 * Does not model physical SCCB or actual OV7725 register latch timing. */
#define main historical_diagnostics
#include "test_v208_zoom_diag.c"
#undef main
static unsigned waits, freezes, gains, scenario;
static uint8_t sync_wr(uint8_t a,uint8_t v){
    if(a==0x13u){++freezes;assert(v==0xf8);}
    return wr(a,v);
}
static uint8_t sync_wait(void){
    assert(reg[0x13]==0xf8);assert(freezes==1);++waits;gains=0;
    if(scenario==2)return 0;
    if(scenario==3)reg[0x13]=0xff;
    if(scenario==4 && waits==2)return 0;
    if(scenario==5 && waits==2)reg[0x02]=0x80;
    /* A deferred sensor update completes at the first boundary. */
    if(scenario==6){reg[0x10]=91;reg[0]=0x31;reg[2]=0x70;}
    return 1;
}
static uint8_t sync_rd(uint8_t a,uint8_t *v){
    if(!rd(a,v))return 0;
    if(a==0 && waits && ++gains==2 && (scenario==1||scenario==4||(scenario==0&&waits==1))) *v^=1u;
    if(a==0x02 && scenario==7 && waits==1)return 0;
    return 1;
}
static void setup(unsigned sc){seed(84,0x7f);waits=freezes=gains=0;scenario=sc;}
int main(void){
    c3_exposure_t s;c3_zoom_hold_t h;
    c3_exposure_init(&s,9,0);
    for(unsigned sc=0;sc<8;++sc){
        setup(sc);if(sc==5)reg[0x02]=0x3f;
        memset(&h,0xff,sizeof(h));
        uint8_t ok=c3_zoom_hold_read_synced(&h,&s,sync_rd,sync_wr,sync_wait);
        assert(freezes==1);
        if(sc==1){assert(!ok&&!h.source.valid&&waits==3&&c3_zoom_read_failure==15);}
        else if(sc==2){assert(!ok&&!h.source.valid&&waits==1&&c3_zoom_read_failure==19);}
        else if(sc==3){assert(!ok&&!h.source.valid&&waits==1&&c3_zoom_read_failure==11);}
        else if(sc==4){assert(!ok&&!h.source.valid&&waits==2&&c3_zoom_read_failure==19);}
        else {assert(ok&&h.source.valid&&c3_zoom_read_failure==0);assert(waits==(sc==6?1u:2u));}
        if(sc==6){assert(h.source.preview_rows==91&&h.source.gain_register==0x31&&h.red==0x70);}
    }
    setup(0);blocked=0x13;assert(!c3_zoom_hold_read_synced(&h,&s,sync_rd,sync_wr,sync_wait)&&!h.source.valid&&waits==0&&c3_zoom_read_failure==11);
    setup(0);reg[0x02]=0x3f;assert(!c3_zoom_hold_read_synced(&h,&s,sync_rd,sync_wr,sync_wait)&&!h.source.valid&&waits==3);
    /* Test every manual dial/gain combination with stable snapshots. */
    unsigned count=0;
    for(unsigned dial=0;dial<9;dial++)for(unsigned gain=0;gain<6;gain++){
        setup(6);c3_exposure_init(&s,(uint8_t)dial,(uint8_t)gain);
        assert(c3_zoom_hold_read_synced(&h,&s,sync_rd,sync_wr,sync_wait)&&h.source.valid&&waits==1&&freezes==1);
        assert(c3_zoom_hold_matches(&h,&s));++count;
    }
    printf("PASS 10 snapshot recovery/failure cases and %u manual setting combinations; lock published only on success\n",count);
    return 0;
}

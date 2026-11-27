#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_fifo_dma_margin_plan.h"
int main(void) {
    unsigned timing=0, good=0;
    const unsigned expected_freq[]={400,450,480,514,554,600};
    for(unsigned m=0;m<C3_FM_NMODE;m++)for(unsigned f=0;f<C3_FM_NFREQ;f++)for(unsigned p=0;p<C3_FM_NPHASE;p++) {
        c3_fd_plan_t t=c3_fm_plan(f,p);
        assert(c3_fm_freq100(t)==expected_freq[f]);
        assert(t.high_ticks+2u==t.ticks && t.sample_tick==p+1u && t.sample_tick<t.high_ticks);
        assert(c3_fm_quiet_cycles(t)==t.ticks*640u+144u);
        for(unsigned delay=0;delay<2u*t.ticks;delay++) {
            unsigned n=C3_FD_BYTES+delay/t.ticks,cnt=delay%t.ticks;
            for(unsigned ndtr=0;ndtr<=2;ndtr++)for(unsigned te=0;te<2;te++)
            for(unsigned timeout=0;timeout<2;timeout++)for(unsigned tc=0;tc<2;tc++)
            for(unsigned stable=0;stable<2;stable++)for(unsigned pin=0;pin<2;pin++) {
                unsigned later=stable?cnt:(cnt+1)%t.ticks;
                int ok=!te&&!timeout&&n==C3_FD_BYTES&&cnt<t.high_ticks&&stable&&!ndtr&&tc&&pin;
                assert((c3_fd_check(te,timeout,n,cnt,later,t.high_ticks,ndtr,tc,pin)==C3_FD_OK)==ok);
                timing++; good+=ok;
            }
        }
    }
    assert(c3_fm_plan(6,0).ticks==0&&c3_fm_plan(0,4).ticks==0);
    assert(C3_FM_REPEATS==32u);
    uint8_t a[640],b[640];
    for(unsigned i=0;i<640;i++)a[i]=(uint8_t)(i*53u+(i/256u));
    memcpy(b,a,640);c3_fm_diff_t d=c3_fm_diff(a,b);assert(!d.count&&d.first==640);
    for(unsigned i=0;i<640;i++) {
        b[i]^=1;d=c3_fm_diff(a,b);assert(d.count==1&&d.first==i&&d.expected==a[i]&&d.actual==b[i]);b[i]^=1;
    }
    memcpy(b,a+1,639);b[639]=(uint8_t)(a[639]^1);d=c3_fm_diff(a,b);assert(d.left==0&&d.count>0);
    memcpy(b+1,a,639);b[0]=(uint8_t)(a[0]^1);d=c3_fm_diff(a,b);assert(d.right==0&&d.count>0);
    printf("PASS: 48 plans; %u abstract stop/transfer observations (%u accepted); 640 single-byte mismatch positions, both shift directions\n",timing,good);
}

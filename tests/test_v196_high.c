#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "c3_fifo_dma_high_plan.h"
int main(void) {
    const unsigned freq[]={600,655,720,800,900,1029,1200};
    const unsigned periods[]={12,11,10,9,8,7,6};
    const unsigned pulse_cases[]={0,639,640,641,65535};
    const unsigned ndtr_cases[]={0,1,2,320,640};
    unsigned plans=0, checks=0, accepted=0;
    for(unsigned m=0;m<C3_FH_NMODE;m++)for(unsigned f=0;f<C3_FH_NFREQ;f++)for(unsigned p=0;p<C3_FH_NPHASE;p++) {
        c3_fd_plan_t t=c3_fh_plan(f,p);plans++;
        assert(c3_fm_freq100(t)==freq[f] && t.ticks==periods[f]);
        assert(t.high_ticks+2==t.ticks && t.sample_tick==p+1 && t.sample_tick<t.ticks);
        assert(c3_fm_quiet_cycles(t)==t.ticks*640u+144u);
        for(unsigned pi=0;pi<5;pi++)for(unsigned cnt=0;cnt<t.ticks;cnt++)
        for(unsigned ni=0;ni<5;ni++)for(unsigned te=0;te<2;te++)for(unsigned timeout=0;timeout<2;timeout++)
        for(unsigned tc=0;tc<2;tc++)for(unsigned stable=0;stable<2;stable++)for(unsigned pin=0;pin<2;pin++) {
            unsigned pulses=pulse_cases[pi], ndtr=ndtr_cases[ni], later=stable?cnt:(cnt+1)%t.ticks;
            unsigned want = te?1:pulses<640?2:pulses>640?3:(!stable||cnt>=t.high_ticks)?4:(ndtr||!tc)?5:!pin?6:timeout?2:0;
            unsigned got=c3_fh_check(te,timeout,pulses,cnt,later,t.high_ticks,ndtr,tc,pin);
            assert(got==want); checks++;accepted+=got==0;
        }
        /* Typical v194 E2 failure: the gate stopped correctly but DMA
         * never reached zero; v196 must expose missing transfer as E5. */
        assert(c3_fd_check(0,1,640,0,0,t.high_ticks,1,0,1)==C3_FD_TIMEOUT);
        assert(c3_fh_check(0,1,640,0,0,t.high_ticks,1,0,1)==C3_FD_NDTR);
        assert(c3_fh_check(0,0,639,0,0,t.high_ticks,1,0,1)==C3_FD_TIMEOUT);
        assert(c3_fh_check(0,1,641,0,0,t.high_ticks,1,0,1)==C3_FD_COUNT);
        assert(c3_fh_check(0,1,640,0,0,t.high_ticks,0,1,1)==C3_FD_TIMEOUT);
    }
    assert(plans==56&&C3_FH_REPEATS==32u);
    assert(c3_fh_plan(7,0).ticks==0&&c3_fh_plan(0,4).ticks==0);
    printf("PASS: %u plans, %u abstract hardware snapshots (%u accepted); old E2 distinguished from short DMA (E5) and count overrun (E3)\n",plans,checks,accepted);
}

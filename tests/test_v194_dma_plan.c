#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "c3_fifo_dma_plan.h"

/* Abstract timing model: not a simulation of STM32 trigger latency.  It
 * enumerates uncertain gating/transfer delays and checks acceptance of
 * actual observed counters. The model never certifies hardware. */
int main(void)
{
    unsigned timing=0,good=0,rejected=0;
    for(unsigned f=0;f<C3_FD_NFREQ;f++) for(unsigned p=0;p<C3_FD_NPHASE;p++) {
        c3_fd_plan_t t=c3_fd_plan(f,p);
        assert(t.ticks*t.mhz==72u);
        assert(t.high_ticks+2u==t.ticks);
        assert(t.sample_tick>=2u && t.sample_tick<t.ticks);
        /* Test every final counter value around the gate crossing, every
         * DMA remaining-count and status condition, pin state, and
         * stable/nonstable gate observation. */
        for(unsigned delay=0;delay<2u*t.ticks;delay++) {
            unsigned n=C3_FD_BYTES+delay/t.ticks,cnt=delay%t.ticks;
            for(unsigned ndtr=0;ndtr<=2;ndtr++) for(unsigned te=0;te<2;te++)
            for(unsigned timeout=0;timeout<2;timeout++) for(unsigned tc=0;tc<2;tc++)
            for(unsigned stable=0;stable<2;stable++) for(unsigned pin=0;pin<2;pin++) {
                unsigned later=stable?cnt:(cnt+1)%t.ticks;
                int ok=!te&&!timeout&&n==C3_FD_BYTES&&cnt<t.high_ticks&&stable&&!ndtr&&tc&&pin;
                uint8_t st=c3_fd_check(te,timeout,n,cnt,later,t.high_ticks,ndtr,tc,pin);
                assert((st==C3_FD_OK)==ok);
                timing++;if(ok)good++;else rejected++;
            }
        }
    }
    assert(c3_fd_plan(6,0).ticks==0 && c3_fd_plan(0,3).ticks==0);
    /* Every GPIO lower halfword including unrelated PG bits. */
    for(unsigned v=0;v<65536u;v++) assert(c3_fd_unpack((uint16_t)v)==((v&0x1feu)/2u));
    assert(c3_fd_check(1,0,640,0,0,4,0,1,1)==C3_FD_DMA_ERROR);
    assert(c3_fd_check(0,1,640,0,0,4,0,1,1)==C3_FD_TIMEOUT);
    assert(c3_fd_check(0,0,641,0,0,4,0,1,1)==C3_FD_COUNT);
    assert(c3_fd_check(0,0,640,4,4,4,0,1,1)==C3_FD_STOP);
    assert(c3_fd_check(0,0,640,0,0,4,1,0,1)==C3_FD_NDTR);
    assert(c3_fd_check(0,0,640,0,0,4,0,1,0)==C3_FD_PIN);
    printf("PASS: 18 frequency/phase plans; %u abstract stop/transfer observations (%u accepted, %u rejected); 65536 GPIO halfwords\n",timing,good,rejected);
    return 0;
}

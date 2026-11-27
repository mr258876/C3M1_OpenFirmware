#ifndef C3_FIFO_DMA_HIGH_PLAN_H
#define C3_FIFO_DMA_HIGH_PLAN_H
#include "c3_fifo_dma_margin_plan.h"
#define C3_FH_NFREQ 7u
#define C3_FH_NPHASE 4u
#define C3_FH_NMODE 2u
#define C3_FH_REPEATS 32u
/* 72 MHz, two low ticks, same waveform as v195. T4 at 12 MHz
 * is on the falling edge, intentionally included as a boundary test. */
static inline c3_fd_plan_t c3_fh_plan(unsigned f,unsigned p)
{
    static const uint8_t period[C3_FH_NFREQ]={12,11,10,9,8,7,6};
    c3_fd_plan_t t={0};
    if(f>=C3_FH_NFREQ || p>=C3_FH_NPHASE) return t;
    t.ticks=period[f]; t.high_ticks=t.ticks-2u;
    t.sample_tick=p+1u; return t;
}
/* Classify final hardware state rather than let generic timeout mask
 * a short DMA transfer or gate overrun. All failures remain failures. */
static inline uint8_t c3_fh_check(unsigned te,unsigned timeout,unsigned pulses,
                                  unsigned cnt,unsigned later,unsigned high_ticks,
                                  unsigned ndtr,unsigned tc,unsigned pin_high)
{
    if(te) return C3_FD_DMA_ERROR;
    if(pulses<C3_FD_BYTES) return C3_FD_TIMEOUT;
    if(pulses>C3_FD_BYTES) return C3_FD_COUNT;
    if(cnt!=later || cnt>=high_ticks) return C3_FD_STOP;
    if(ndtr || !tc) return C3_FD_NDTR;
    if(!pin_high) return C3_FD_PIN;
    if(timeout) return C3_FD_TIMEOUT;
    return C3_FD_OK;
}
#endif

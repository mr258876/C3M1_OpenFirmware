#ifndef C3_FIFO_DMA_LIVE_PLAN_H
#define C3_FIFO_DMA_LIVE_PLAN_H
#include "c3_fifo_dma_plan.h"
/* Fixed T1 with a two-tick low pulse. */
static inline uint8_t c3_live_check(unsigned n,unsigned te,unsigned pulses,
    unsigned cnt,unsigned later,unsigned high_ticks,unsigned ndtr,unsigned tc,unsigned pin_high)
{
    if(te)return C3_FD_DMA_ERROR;
    if(pulses<n)return C3_FD_TIMEOUT;
    if(pulses>n)return C3_FD_COUNT;
    if(cnt!=later || cnt>=high_ticks)return C3_FD_STOP;
    if(ndtr || !tc)return C3_FD_NDTR;
    if(!pin_high)return C3_FD_PIN;
    return C3_FD_OK;
}
static inline unsigned c3_live_slot(unsigned b){return b&1u;}
/* Pack two GPIO IDR halfwords into two original bytes, no RGB byte swap. */
static inline uint16_t c3_live_unpack_pair(uint32_t v)
{ return (uint16_t)(((v>>1u)&0xffu)|((v>>9u)&0xff00u)); }
#endif

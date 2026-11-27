#ifndef C3_FIFO_DMA_PLAN_H
#define C3_FIFO_DMA_PLAN_H
#include <stdint.h>
#define C3_FD_ROWS 240u
#define C3_FD_BYTES 640u
#define C3_FD_NFREQ 6u
#define C3_FD_NPHASE 3u
/* 72MHz timer. PWM1 output stays HIGH at idle (pre-edge CPU reader).
 * Last two ticks LOW. DMA request after previous rising edge; phase is
 * request position, NOT actual GPIO sample (APB/AHB arbitration delay). */
typedef struct { uint8_t mhz, ticks, high_ticks, sample_tick; } c3_fd_plan_t;
static inline c3_fd_plan_t c3_fd_plan(unsigned f, unsigned p)
{
    static const uint8_t mhz[C3_FD_NFREQ]={3,4,6,8,9,12};
    c3_fd_plan_t t={0};
    if(f>=C3_FD_NFREQ || p>=C3_FD_NPHASE) return t;
    t.mhz=mhz[f]; t.ticks=72u/t.mhz; t.high_ticks=t.ticks-2u;
    t.sample_tick=(uint8_t)(t.ticks*(p+1u)/4u);
    if(t.sample_tick<2u) t.sample_tick=2u;
    return t;
}
/* Stop/transfer checks are also used by the host reference tests. */
enum { C3_FD_OK=0, C3_FD_DMA_ERROR=1, C3_FD_TIMEOUT=2,
       C3_FD_COUNT=3, C3_FD_STOP=4, C3_FD_NDTR=5,
       C3_FD_PIN=6, C3_FD_REFERENCE=7, C3_FD_DATA=8,
       C3_FD_CLOCK=9 };
static inline uint8_t c3_fd_check(unsigned te,unsigned timeout,unsigned pulses,
                                  unsigned cnt,unsigned cnt_later,unsigned high_ticks,
                                  unsigned ndtr,unsigned tc,unsigned pin_high)
{
    if(te) return C3_FD_DMA_ERROR;
    if(timeout) return C3_FD_TIMEOUT;
    if(pulses!=C3_FD_BYTES) return C3_FD_COUNT;
    if(cnt!=cnt_later || cnt>=high_ticks) return C3_FD_STOP;
    if(ndtr || !tc) return C3_FD_NDTR;
    if(!pin_high) return C3_FD_PIN;
    return C3_FD_OK;
}
static inline uint8_t c3_fd_unpack(uint16_t sample)
{ return (uint8_t)((sample>>1u)&0xffu); }
#endif

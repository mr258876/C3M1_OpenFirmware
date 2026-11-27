#ifndef C3_FIFO_DMA_LOAD_PLAN_H
#define C3_FIFO_DMA_LOAD_PLAN_H
#include "c3_fifo_dma_high_plan.h"
#define C3_FL_NFREQ 7u
#define C3_FL_NPHASE 2u
#define C3_FL_NMODE 4u
#define C3_FL_REPEATS 32u
enum { C3_FL_QUIET=0, C3_FL_CPU=1, C3_FL_LCD=2, C3_FL_BOTH=3,
       C3_FL_LCD_ERROR=10, C3_FL_NO_OVERLAP=11 };
static inline c3_fd_plan_t c3_fl_plan(unsigned f,unsigned p)
{
    static const uint8_t ticks[C3_FL_NFREQ]={18,16,15,14,13,12,11};
    c3_fd_plan_t t={0};
    if(f>=C3_FL_NFREQ || p>=C3_FL_NPHASE)return t;
    t.ticks=ticks[f]; t.high_ticks=t.ticks-2u; t.sample_tick=p+1u; return t;
}
/* At iteration y: FIFO fills raw[y&1], CPU reads raw[(y-1)&1],
 * LCD reads out[(y-2)&1]. CPU destination and LCD source differ. */
static inline unsigned c3_fl_slot(unsigned y){return y&1u;}
#endif

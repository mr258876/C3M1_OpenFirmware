#ifndef C3_FIFO_DMA_MARGIN_PLAN_H
#define C3_FIFO_DMA_MARGIN_PLAN_H
#include "c3_fifo_dma_plan.h"
#define C3_FM_NFREQ 6u
#define C3_FM_NPHASE 4u
#define C3_FM_NMODE 2u
#define C3_FM_REPEATS 32u
/* Unchanged two-tick LOW interval. CNT=1..4 includes previous
 * 4MHz EARLY and 6MHz EARLY control points. */
static inline c3_fd_plan_t c3_fm_plan(unsigned f,unsigned p)
{
    static const uint8_t period[C3_FM_NFREQ]={18,16,15,14,13,12};
    c3_fd_plan_t t={0};
    if(f>=C3_FM_NFREQ || p>=C3_FM_NPHASE) return t;
    t.ticks=period[f]; t.high_ticks=t.ticks-2u;
    t.sample_tick=p+1u; return t;
}
static inline unsigned c3_fm_freq100(c3_fd_plan_t t)
{ return t.ticks?(7200u+t.ticks/2u)/t.ticks:0u; }
/* DWT cycle delay AFTER timer start. Final hardware checks unchanged. */
static inline uint32_t c3_fm_quiet_cycles(c3_fd_plan_t t)
{ return (uint32_t)t.ticks*C3_FD_BYTES+144u; }
typedef struct {
    uint16_t count, first, left, right;
    uint8_t expected, actual;
} c3_fm_diff_t;
/* Directional shift counts compare one fewer byte; low values are a
 * diagnostic hint for alignment, not an automatic error classification. */
static inline c3_fm_diff_t c3_fm_diff(const uint8_t *ref,const uint8_t *got)
{
    c3_fm_diff_t d={0};d.first=C3_FD_BYTES;
    for(unsigned x=0;x<C3_FD_BYTES;x++) {
        if(ref[x]!=got[x]) {
            ++d.count;
            if(d.first==C3_FD_BYTES) {
                d.first=x;d.expected=ref[x];d.actual=got[x];
            }
        }
        if(x+1u<C3_FD_BYTES) {
            d.left+=got[x]!=ref[x+1u];
            d.right+=got[x+1u]!=ref[x];
        }
    }
    return d;
}
#endif

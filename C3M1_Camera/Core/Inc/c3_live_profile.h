#ifndef C3_LIVE_PROFILE_H
#define C3_LIVE_PROFILE_H
#include <stdint.h>
#ifndef C3_LIVE_PROFILE
#define C3_LIVE_PROFILE 0
#endif
#if C3_LIVE_PROFILE
typedef struct {
    uint32_t unpack,peak,score,cpu,wait,lcd,draw,max_block,max_overlap,over,overlap,valid;
    uint32_t max_peak_overlap,peak_over,peak_overlap;
    uint8_t area,peaking,mode,sample,shared;
} c3_live_profile_t;
extern c3_live_profile_t c3_live_profile;
/* Remaining nominal transfer interval after rendering, not idle outside draw. */
static inline uint32_t c3_live_slack(uint32_t elapsed,uint32_t budget)
{ return elapsed<budget?budget-elapsed:0u; }
#endif
#endif

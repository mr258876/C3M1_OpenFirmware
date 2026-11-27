#ifndef C3_POSTSHOT_H
#define C3_POSTSHOT_H
#include <stdint.h>
#include "c3_board_pins.h"
/* Timer begins AFTER the saved photo is rendered. Only fresh debounced
 * presses from the modal page are accepted; no exit event is replayed. */
enum { C3_POST_DONE=0, C3_POST_REVIEW, C3_POST_WIND };
typedef struct { uint32_t start; uint8_t state,wind; } c3_postshot_t;
static inline c3_postshot_t c3_postshot_begin(uint8_t review,uint8_t wind,uint32_t now)
{
    /* Winding takes priority: the caller has already displayed the photo
     * (if enabled). Show the wind hint now, with no timed review phase. */
    c3_postshot_t s={now,wind?C3_POST_WIND:review?C3_POST_REVIEW:C3_POST_DONE,!!wind};
    return s;
}
static inline void c3_postshot_poll(c3_postshot_t *s,uint16_t pressed,uint32_t now)
{
    if(s->state==C3_POST_DONE)return;
    if(s->wind?(pressed&C3_BTN_FILM):(pressed&C3_BTN_MASK)){
        s->state=C3_POST_DONE;return;
    }
    if(s->state==C3_POST_REVIEW&&(uint32_t)(now-s->start)>=5000u)
        s->state=s->wind?C3_POST_WIND:C3_POST_DONE;
}
#endif

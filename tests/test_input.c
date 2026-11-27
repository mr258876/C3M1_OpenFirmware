#include "c3_board_pins.h"
#include "c3_input.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define ISO C3_BTN_ISO
#define ZOOM C3_BTN_4X
#define FILM C3_BTN_FILM

static void samples(uint16_t mask,unsigned from,unsigned to)
{
    for (unsigned t=from;t<=to;t+=5u)
        c3_input_sample(mask,t);
}
int main(void)
{
    /* Short 70ms 4X press must survive a 300ms LCD draw: event queue is
     * serviced in SysTick, not only in the slow foreground frame loop. */
    c3_input_reset(0u,0u);
    samples(0u,0u,20u);
    samples(ZOOM,25u,95u);
    samples(0u,100u,150u);
    c3_input_batch_t b=c3_input_take();
    assert((b.events.pressed & ZOOM) && (b.events.short_released & ZOOM));
    assert(!(b.events.long_pressed & ZOOM));
    assert(b.stable==0u && b.film_chords==0u);
    b=c3_input_take();
    assert(!b.events.pressed && !b.events.short_released);

    /* ISO plus lever modifier must not be dispatched to the preview as an
     * unrelated ISO gain step, even if all keys were released before take. */
    c3_input_reset(0u,200u);
    samples(FILM,205u,235u);
    samples(FILM|ISO,240u,300u);
    samples(FILM,305u,340u);
    samples(0u,345u,390u);
    b=c3_input_take();
    assert((b.events.short_released & ISO) != 0u);
    assert((b.film_chords & ISO) != 0u);
    assert(b.stable==0u);

    /* Long lever >=650ms is latched exactly once, and short tap does not
     * issue long_press. This is the only way to request the MODE page. */
    c3_input_reset(0u,400u);
    samples(FILM,405u,1105u);
    b=c3_input_take();
    assert((b.events.pressed & FILM) != 0u);
    assert((b.events.long_pressed & FILM) != 0u);
    samples(FILM,1110u,1200u);
    b=c3_input_take();
    assert((b.events.long_pressed & FILM) == 0u);
    samples(0u,1205u,1240u);
    b=c3_input_take();
    assert((b.events.short_released & FILM)==0u);
    assert((b.events.long_released & FILM)!=0u);

    c3_input_reset(0u,1300u);
    samples(FILM,1305u,1355u);
    samples(0u,1360u,1390u);
    b=c3_input_take();
    assert((b.events.short_released & FILM) &&
           !(b.events.long_pressed & FILM));
    printf("Input 200Hz verified: fast 4X tap, FILM/ISO chord, 650ms lever, event latch\n");
    return 0;
}

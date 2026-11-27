/* Host-only behavioural check: exposure-only changes update registers while
 * flushed FIFO state cannot return an old preview frame after rearm. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "c3_exposure.h"
#include "c3_frame_sched.h"
static uint8_t regs[256];
static unsigned writes;
static uint8_t spy_write(uint8_t reg,uint8_t val)
{
    regs[reg] = val;
    ++writes;
    return 0u;
}
int main(void)
{
    c3_frame_sched_t q;
    c3_exposure_t e;
    c3_exposure_init(&e,8u,0u);
    e.shoot_mode=C3_SHOOT_M;
    e.gain_step=2u;
    c3_sched_reset(&q,C3_SCHED_PREVIEW);
    assert(c3_sched_vsync(&q)); /* old frame currently writing */
    c3_sched_reset(&q,C3_SCHED_OFF); /* stop during reconfiguration */
    assert(!c3_sched_vsync(&q));
    assert(c3_exposure_apply(&e,C3_EXPOSURE_PREVIEW,spy_write));
    assert(writes>0u);
    assert(regs[0x13u]==0xFAu && regs[0x00u]==0x10u);
    assert(regs[0x10u]==(uint8_t)c3_exposure_rows(8u));
    c3_sched_reset(&q,C3_SCHED_PREVIEW); /* same real FIFO rearm action */
    assert(!c3_sched_acquire(&q));
    assert(c3_sched_vsync(&q));
    assert(!c3_sched_acquire(&q));
    assert(c3_sched_vsync(&q));
    assert(c3_sched_acquire(&q));
    c3_sched_release(&q);
    /* Repeating in-place changes must not require sensor reset. */
    writes=0u;
    e.gain_step=3u;
    assert(c3_exposure_apply(&e,C3_EXPOSURE_PREVIEW,spy_write));
    assert(writes>0u && regs[0x00u]==0x30u);
    puts("v166 host preview controls: register update and FIFO flush PASS");
    return 0;
}

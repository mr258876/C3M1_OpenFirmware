#include "c3_sleep.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
int main(void)
{
    assert(C3_SD_LED_HOLD_MS==180u);
    /* Power-on and idle are dark; in-flight writes remain lit. */
    assert(c3_sd_led_should_light(0,0,42u,0u)==0u);
    assert(c3_sd_led_should_light(1,0,99999u,0u)==1u);
    assert(c3_sd_led_should_light(1,1,1000u,0u)==1u);
    /* 180 ms minimum visibility, without adding disk I/O delay. */
    for (uint32_t t=100u;t<280u;++t)
        assert(c3_sd_led_should_light(0,1,t,100u)==1u);
    assert(c3_sd_led_should_light(0,1,280u,100u)==0u);
    /* SysTick 32-bit wraparound never sticks the LED on. */
    assert(c3_sd_led_should_light(0,1,0x00000020u,0xFFFFFFE0u)==1u);
    assert(c3_sd_led_should_light(0,1,0x00000094u,0xFFFFFFE0u)==0u);
    /* Stale valid bit only has meaning until the timeout. */
    assert(c3_sd_led_should_light(0,1,1000u,0u)==0u);
    puts("v161 SD LED: idle off, transfer on, 180ms grace, SysTick wrap PASS");
    return 0;
}

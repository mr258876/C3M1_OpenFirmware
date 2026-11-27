#include "c3_sleep.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
int main(void)
{
    static const uint32_t ms[]={0u,15000u,30000u,60000u,180000u};
    for (unsigned i=0;i<5u;++i) {
        assert(c3_sleep_timeout_ms((uint8_t)i)==ms[i]);
        if (i) {
            uint32_t start=0xFFFFF000u;
            assert(!c3_sleep_due(i,start+ms[i]-1u,start,0u));
            assert(c3_sleep_due(i,start+ms[i],start,0u));
            assert(!c3_sleep_due(i,start+ms[i],start,2u));
        }
    }
    assert(c3_sleep_timeout_ms(5u)==0u);
    assert(c3_sleep_timeout_ms(255u)==0u);
    assert(!c3_sleep_due(0u,100000u,0u,0u));
    assert(c3_sleep_breath_us(0u)==0u);
    assert(c3_sleep_breath_us(5000u)==0u);
    assert(c3_sleep_breath_us(2500u)==3000u);
    assert(c3_sleep_breath_us(7500u)==3000u);
    uint16_t prev=0u;
    for (uint32_t t=1u;t<=2500u;t+=1u) {
        uint16_t v=c3_sleep_breath_us(t);
        assert(v>=prev && v<=3000u);
        assert(v==c3_sleep_breath_us(5000u-t));
        prev=v;
    }
    puts("Sleep: timeouts, disabled default, wrap, held keys, 5s smooth breathing PASS");
    return 0;
}

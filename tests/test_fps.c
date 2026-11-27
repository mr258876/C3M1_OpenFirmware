#include "c3_fps.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
int main(void)
{
    c3_fps_t f;
    c3_fps_reset(&f);
    assert(f.tenths==0u && f.started==0u);
    assert(!c3_fps_frame(&f, 100u));
    for (uint32_t k=1;k<=9u;k++) assert(!c3_fps_frame(&f,100u+k*100u));
    assert(c3_fps_frame(&f,1100u));
    assert(f.tenths==100u); /* 10.0 actual displayed frames/sec */
    c3_fps_reset(&f);
    (void)c3_fps_frame(&f,0u);
    for (uint32_t k=1;k<25u;k++) assert(!c3_fps_frame(&f,k*40u));
    assert(c3_fps_frame(&f,1000u) && f.tenths==250u);
    c3_fps_reset(&f);
    (void)c3_fps_frame(&f,UINT32_MAX-499u);
    assert(c3_fps_frame(&f,500u) && f.tenths==10u); /* wrap, ~1 frame/s */
    c3_fps_reset(&f);
    (void)c3_fps_frame(&f,0u);
    assert(c3_fps_frame(&f,10000u) && f.tenths==1u); /* 0.1 fps */
    printf("FPS verified: 10.0, 25.0, wraparound, 0.1 fps\n");
    return 0;
}

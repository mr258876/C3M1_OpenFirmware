/* Streamed focus estimator regression, independent of the MCU peripherals. */
#include "c3_focus.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

static uint16_t gray565(unsigned x)
{
    unsigned r5 = x >> 1u;
    if (r5 > 31u) r5=31u;
    if (x > 63u) x=63u;
    return (uint16_t)((r5 << 11) | (x << 5) | r5);
}

static unsigned render(c3_focus_t *s, unsigned kind)
{
    unsigned peaks = 0u;
    c3_focus_begin_frame(s);
    for (unsigned y = 0u; y < 240u; ++y) {
        for (unsigned x = 0u; x < 320u; ++x) {
            unsigned level=28u;
            if (kind==1u) level=((x / 8u + y / 8u) & 1u) ? 62u : 3u;
            if (kind==2u) level=(x / 8u) & 31u;
            if (kind==3u && (x < C3_FOCUS_ROI_X0 || x >= C3_FOCUS_ROI_X1 ||
                              y < C3_FOCUS_ROI_Y0 || y >= C3_FOCUS_ROI_Y1))
                level=((x / 8u + y / 8u) & 1u) ? 62u : 3u;
            peaks += c3_focus_sample(s, (uint16_t)x, (uint16_t)y, gray565(level),s->threshold);
        }
    }
    c3_focus_end_frame(s);
    return peaks;
}
int main(void)
{
    c3_focus_t s;
    c3_focus_reset(&s);
    assert(s.threshold==24u && s.score==0u);
    assert(render(&s,0u)==0u);
    assert(s.score==0u && s.threshold>=22u);
    c3_focus_reset(&s);
    unsigned peaks = render(&s,1u);
    uint8_t sharp_score=s.score;
    assert(peaks>250u && sharp_score>25u);
    assert(s.threshold>=22u && s.threshold<=55u);
    for (unsigned k=0;k<4u;k++) render(&s,1u);
    assert(s.score>25u);
    c3_focus_reset(&s);
    (void)render(&s,2u);
    assert(s.score<sharp_score);
    c3_focus_reset(&s);
    (void)render(&s,3u);
    assert(s.score==0u); /* contrast outside centre must not enter score */
    assert(c3_focus_roi_corner(80u,60u));
    assert(c3_focus_roi_corner(239u,179u));
    assert(!c3_focus_roi_corner(159u,100u));
    assert(!c3_focus_roi_corner(80u,100u));
    /* At 4x, focus is evaluated once per 4x4 displayed block (80x60
     * source pixels); the centre ROI therefore has only 40x30 samples.
     * Verify that contrast scores are normalised to the sample count. */
    c3_focus_reset(&s);
    c3_focus_begin_frame(&s);
    for (unsigned zy=0u;zy<60u;++zy)
        for (unsigned zx=0u;zx<80u;++zx) {
            unsigned value=((zx/2u + zy/2u)&1u) ? 62u : 3u;
            (void)c3_focus_sample(&s,(uint16_t)(zx*4u),
                        (uint16_t)(zy*4u),gray565(value),s.threshold);
        }
    c3_focus_end_frame_area(&s,C3_FOCUS_ROI_AREA/16u);
    assert(s.score>10u); /* Would be incorrectly near zero if /19200. */
    printf("Focus stream verified: 320B row, ROI=%u pixels, strong peaks=%u, sharp score=%u\n",
           (unsigned)C3_FOCUS_ROI_AREA, peaks, sharp_score);
    return 0;
}

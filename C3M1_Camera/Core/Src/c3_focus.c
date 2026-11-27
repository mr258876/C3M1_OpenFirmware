#include "c3_focus.h"
#include <string.h>

/* Streaming focus state. */
c3_focus_t c3_focus_state;

void c3_focus_reset(c3_focus_t *s)
{
    memset(s, 0, sizeof(*s));
    s->threshold = 24u;
}
void c3_focus_begin_frame(c3_focus_t *s)
{
    s->left_luma = 0u;
    s->roi_sum = 0u;
}
void c3_focus_end_frame_area(c3_focus_t *s, uint32_t roi_sample_count)
{
    /* Normalise by actual samples, not enlarged display pixel count. */
    if (roi_sample_count == 0u) roi_sample_count = 1u;
    uint32_t mean = (s->roi_sum + roi_sample_count/2u) / roi_sample_count;
    uint32_t raw_score = mean * 4u;
    uint32_t target_threshold = 16u + mean * 2u;
    if (raw_score > 99u) raw_score = 99u;
    if (target_threshold < 22u) target_threshold = 22u;
    if (target_threshold > 55u) target_threshold = 55u;
    if (!s->settled) {
        s->score = (uint8_t)raw_score;
        s->threshold = (uint8_t)target_threshold;
        s->settled = 1u;
    } else {
        s->score = (uint8_t)((3u * s->score + raw_score + 2u) / 4u);
        s->threshold = (uint8_t)((3u * s->threshold + target_threshold + 2u) / 4u);
    }
}
/* Sparse green-channel focus score; use actual left/up neighbors. */
c3_score_t c3_score_state;

void c3_score_reset(c3_score_t *s)
{
    memset(s, 0, sizeof(*s));
}
void c3_score_begin_frame(c3_score_t *s, uint8_t mode)
{
    mode = c3_score_effective_mode(mode);
    if (s->mode != mode) {
        s->mode = mode;
        s->settled = 0u;
        s->score = 0u;
        s->skip_next = 0u;
    }
    s->sum = 0u;
    s->count = 0u;
}
uint8_t c3_score_schedule_frame(c3_score_t *s, uint8_t mode, uint8_t shared)
{
    c3_score_begin_frame(s, mode);
    if (s->mode == C3_SCORE_OFF) return 0u;
    (void)shared;
    s->skip_next = 0u;
    return 1u;
}
uint8_t c3_score_row_active(uint8_t mode, uint16_t y)
{
    if (mode == C3_SCORE_OFF || mode >= C3_SCORE_COUNT || y >= C3_FOCUS_HEIGHT)
        return 0u;
    if (y < C3_SCORE_SP_Y0-1u || y >= C3_SCORE_SP_Y1) return 0u;
    return (uint8_t)((y & 3u) == 0u || ((y & 3u) == 3u && y+1u < C3_SCORE_SP_Y1));
}
static inline __attribute__((always_inline)) uint8_t score_luma(const uint8_t *p)
{
    return c3_green_luma(p);
}
static inline unsigned score_delta(uint8_t a, uint8_t b)
{
    return (unsigned)__builtin_abs((int)a-(int)b);
}
void c3_score_sample_row(c3_score_t *s, uint16_t y, const uint8_t *raw_row)
{
    uint8_t mode = s->mode;
    if (!c3_score_row_active(mode,y)) return;
    if (y & 3u) {
        for (uint16_t x=C3_SCORE_SP_X0; x<C3_SCORE_SP_X1; x+=C3_SCORE_SPARSE_STEP)
            s->prev_row[x] = score_luma(raw_row+2u*x);
        return;
    }
    uint32_t sum = 0u;
    for (uint16_t x=C3_SCORE_SP_X0; x<C3_SCORE_SP_X1; x+=C3_SCORE_SPARSE_STEP) {
        const uint8_t *q=raw_row+2u*x;
        uint8_t lum=score_luma(q);
        sum+=score_delta(lum,score_luma(q-2))+score_delta(lum,s->prev_row[x]);
    }
    s->sum+=sum;
    s->count+=C3_SCORE_SP_ROW_COUNT;
}
void c3_score_end_frame(c3_score_t *s)
{
    if (!s->mode || !s->count) return;
    uint32_t mean = (s->sum+s->count/2u)/s->count;
    uint32_t score = mean*4u;
    if (score > 99u) score=99u;
    s->score = s->settled ? (uint8_t)((3u*s->score+score+2u)/4u) : (uint8_t)score;
    s->settled = 1u;
}

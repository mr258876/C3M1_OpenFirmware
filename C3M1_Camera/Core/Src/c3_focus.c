#include "c3_focus.h"
#include <string.h>

/* Only one instance is used by the camera, but the functions take a state
 * pointer so the exact algorithm can also be tested on a host computer. */
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

void c3_focus_end_frame(c3_focus_t *s)
{
    c3_focus_end_frame_area(s, C3_FOCUS_ROI_AREA);
}

/* v0.1.90: exact dense luma/gradient, alternate dense frames and
 * sparse 4x4 sampling using the actual left and upper neighbors. */
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
static inline uint8_t score_is_sparse(uint8_t mode)
{
    return (uint8_t)(mode == C3_SCORE_CENTER_SPARSE || mode == C3_SCORE_FULL_SPARSE);
}
uint8_t c3_score_schedule_frame(c3_score_t *s, uint8_t mode, uint8_t shared)
{
    c3_score_begin_frame(s, mode);
    if (s->mode == C3_SCORE_OFF) return 0u;
    if (shared || score_is_sparse(s->mode)) {
        s->skip_next = 0u;
        return 1u;
    }
    /* Preserve the previous visible score on the fast, unscored frame. */
    uint8_t sample = (uint8_t)(!s->skip_next || !s->settled);
    s->skip_next = sample;
    return sample;
}
uint8_t c3_score_row_active(uint8_t mode, uint16_t y)
{
    if (mode == C3_SCORE_OFF || mode >= C3_SCORE_COUNT || y >= C3_FOCUS_HEIGHT)
        return 0u;
#if C3_SCORE_FULL_SP_ONLY
    if (y < C3_SCORE_SP_Y0-1u || y >= C3_SCORE_SP_Y1) return 0u;
    return (uint8_t)((y & 3u) == 0u || ((y & 3u) == 3u && y+1u < C3_SCORE_SP_Y1));
#else
    const uint16_t y0 = c3_score_is_center(mode) ? C3_FOCUS_ROI_Y0 : 1u;
    const uint16_t y1 = c3_score_is_center(mode) ? C3_FOCUS_ROI_Y1 : C3_FOCUS_HEIGHT;
    if (y < y0-1u || y >= y1) return 0u;
    /* Only the actual sampled row and its immediate upper neighbor. */
    if (!score_is_sparse(mode)) return 1u;
    const uint16_t phase=(y-(y0-1u)) & (C3_SCORE_SPARSE_STEP-1u);
    return (uint8_t)(phase == 1u || (phase == 0u && y+1u < y1));
#endif
}
/* Exact factorization of (4*R5 + 5*G6 + 2*B5)/8. Divide only after
 * summing both bytes: no rounding of individual color contributions. */
const uint16_t c3_luma_hi[256] = {
    0, 40, 80, 120, 160, 200, 240, 280, 4, 44, 84, 124, 164, 204, 244, 284,
    8, 48, 88, 128, 168, 208, 248, 288, 12, 52, 92, 132, 172, 212, 252, 292,
    16, 56, 96, 136, 176, 216, 256, 296, 20, 60, 100, 140, 180, 220, 260, 300,
    24, 64, 104, 144, 184, 224, 264, 304, 28, 68, 108, 148, 188, 228, 268, 308,
    32, 72, 112, 152, 192, 232, 272, 312, 36, 76, 116, 156, 196, 236, 276, 316,
    40, 80, 120, 160, 200, 240, 280, 320, 44, 84, 124, 164, 204, 244, 284, 324,
    48, 88, 128, 168, 208, 248, 288, 328, 52, 92, 132, 172, 212, 252, 292, 332,
    56, 96, 136, 176, 216, 256, 296, 336, 60, 100, 140, 180, 220, 260, 300, 340,
    64, 104, 144, 184, 224, 264, 304, 344, 68, 108, 148, 188, 228, 268, 308, 348,
    72, 112, 152, 192, 232, 272, 312, 352, 76, 116, 156, 196, 236, 276, 316, 356,
    80, 120, 160, 200, 240, 280, 320, 360, 84, 124, 164, 204, 244, 284, 324, 364,
    88, 128, 168, 208, 248, 288, 328, 368, 92, 132, 172, 212, 252, 292, 332, 372,
    96, 136, 176, 216, 256, 296, 336, 376, 100, 140, 180, 220, 260, 300, 340, 380,
    104, 144, 184, 224, 264, 304, 344, 384, 108, 148, 188, 228, 268, 308, 348, 388,
    112, 152, 192, 232, 272, 312, 352, 392, 116, 156, 196, 236, 276, 316, 356, 396,
    120, 160, 200, 240, 280, 320, 360, 400, 124, 164, 204, 244, 284, 324, 364, 404,
};
const uint8_t c3_luma_lo[256] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
    32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
    5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
    37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67,
    10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
    42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72,
    15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45,
    47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77,
    20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50,
    52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82,
    25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55,
    57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87,
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
    62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92,
    35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65,
    67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91, 93, 95, 97,
};
static inline __attribute__((always_inline)) uint8_t score_luma(const uint8_t *p)
{
#if C3_FOCUS_GREEN
    return c3_green_luma(p);
#else
    return (uint8_t)((c3_luma_hi[p[0]] + c3_luma_lo[p[1]]) >> 3u);
#endif
}
/* Legacy exact RGB luma test helper, even in GREEN A/B. */
uint8_t c3_score_luma_bytes(uint8_t hi, uint8_t lo)
{
    return (uint8_t)((c3_luma_hi[hi] + c3_luma_lo[lo]) >> 3u);
}
static inline unsigned score_delta(uint8_t a, uint8_t b)
{
    return (unsigned)__builtin_abs((int)a-(int)b);
}
void c3_score_sample_row(c3_score_t *s, uint16_t y, const uint8_t *raw_row)
{
    uint8_t mode = s->mode;
    if (!c3_score_row_active(mode,y)) return;
#if C3_SCORE_FULL_SP_ONLY
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
#else
    const uint8_t center = c3_score_is_center(mode);
    const uint8_t sparse = score_is_sparse(mode);
    const uint16_t x0 = center ? C3_FOCUS_ROI_X0 : 1u;
    const uint16_t x1 = center ? C3_FOCUS_ROI_X1 : C3_FOCUS_W;
    const uint16_t y0 = center ? C3_FOCUS_ROI_Y0 : 1u;
    const uint8_t *p = raw_row+2u*x0;
    uint8_t *prev = s->prev_row+x0;
    const uint8_t *end = raw_row+2u*x1;
    uint32_t sum = 0u;
    if (sparse) {
        if (((y-(y0-1u)) & (C3_SCORE_SPARSE_STEP-1u)) == 0u) {
            for (uint16_t x=x0; x<x1; x+=C3_SCORE_SPARSE_STEP)
                s->prev_row[x] = score_luma(raw_row+2u*x);
            return;
        }
        for (uint16_t x=x0; x<x1; x+=C3_SCORE_SPARSE_STEP) {
            const uint8_t *q=raw_row+2u*x;
            uint8_t lum = score_luma(q);
            sum += score_delta(lum,score_luma(q-2)) + score_delta(lum,s->prev_row[x]);
        }
        s->count += ((uint32_t)x1-x0+C3_SCORE_SPARSE_STEP-1u)/C3_SCORE_SPARSE_STEP;
    } else {
        if (y < y0) {
            for (; p<end; p+=2, ++prev) *prev=score_luma(p);
            return;
        }
        uint8_t left = score_luma(p-2);
        for (; p<end; p+=2, ++prev) {
            uint8_t lum = score_luma(p);
            sum += score_delta(lum,left) + score_delta(lum,*prev);
            *prev = lum;
            left = lum;
        }
        s->count += (uint32_t)x1-x0;
    }
    s->sum += sum;
#endif
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
void c3_score_end_shared_center(c3_score_t *s, uint32_t roi_sum)
{
    /* The dense center peaking path already calculated these exact edges. */
    if (s->mode != C3_SCORE_CENTER) return;
    s->sum = roi_sum;
    s->count = C3_FOCUS_ROI_AREA;
    c3_score_end_frame(s);
}

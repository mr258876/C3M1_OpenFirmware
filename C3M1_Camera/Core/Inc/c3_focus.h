#ifndef C3_FOCUS_H
#define C3_FOCUS_H
#include <stdint.h>
#ifndef C3_FOCUS_GREEN
#define C3_FOCUS_GREEN 0
#endif
static inline __attribute__((always_inline)) uint8_t c3_green_luma(const uint8_t *p)
{ return (uint8_t)(((p[0]&7u)<<3)|(p[1]>>5)); }

/* Streamed focus peaking for QVGA RGB565, no frame buffer. The indicator
 * measures local gradients, NOT absolute focus or an autofocus lock.
 * A fixed 160x120 centre ROI supplies a relative, smoothed contrast score.
 */
enum {
    C3_SCORE_OFF = 0,
    C3_SCORE_CENTER = 1,
    C3_SCORE_CENTER_SPARSE = 2,
    C3_SCORE_FULL = 3,
    C3_SCORE_FULL_SPARSE = 4,
    C3_SCORE_COUNT = 5
};
#ifndef C3_SCORE_FULL_SP_ONLY
#define C3_SCORE_FULL_SP_ONLY 0
#endif
#if C3_SCORE_FULL_SP_ONLY && !C3_FOCUS_GREEN
#error unified full sparse score requires GREEN
#endif
/* Keep persisted v6 enum values. Any previous valid non-OFF setting is ON. */
static inline uint8_t c3_score_effective_mode(uint8_t mode)
{
#if C3_SCORE_FULL_SP_ONLY
    return mode > C3_SCORE_OFF && mode < C3_SCORE_COUNT ? C3_SCORE_FULL_SPARSE : C3_SCORE_OFF;
#else
    return mode < C3_SCORE_COUNT ? mode : C3_SCORE_OFF;
#endif
}
#define C3_SCORE_SPARSE_STEP 4u
/* Align the score to FULL SP peaking's even/even sample lattice. Exclude
 * the two 24-line bands, regardless of OSD visibility, for exact sharing. */
#define C3_SCORE_SP_X0 4u
#define C3_SCORE_SP_X1 320u
#define C3_SCORE_SP_Y0 24u
#define C3_SCORE_SP_Y1 216u
#define C3_SCORE_SP_ROW_COUNT ((C3_SCORE_SP_X1-C3_SCORE_SP_X0)/C3_SCORE_SPARSE_STEP)
#define C3_SCORE_SP_COUNT (C3_SCORE_SP_ROW_COUNT*((C3_SCORE_SP_Y1-C3_SCORE_SP_Y0)/C3_SCORE_SPARSE_STEP))
#define C3_FOCUS_W 320u
#define C3_FOCUS_HEIGHT 240u
#define C3_FOCUS_ROI_X0 80u
#define C3_FOCUS_ROI_X1 240u  /* exclusive */
#define C3_FOCUS_ROI_Y0 60u
#define C3_FOCUS_ROI_Y1 180u  /* exclusive */
#define C3_FOCUS_ROI_AREA ((C3_FOCUS_ROI_X1-C3_FOCUS_ROI_X0) * (C3_FOCUS_ROI_Y1-C3_FOCUS_ROI_Y0))

typedef struct {
    uint8_t prev_row[C3_FOCUS_W];
    uint8_t left_luma;
    uint8_t threshold;        /* adaptive gradient threshold, 6-bit luma scale */
    uint8_t score;            /* smoothed relative contrast, 0..99 */
    uint8_t settled;
    uint32_t roi_sum;
} c3_focus_t;

extern c3_focus_t c3_focus_state;

/* Independent streamed score. Peaking retains its own adaptive threshold
 * and ROI accumulator. Sample original sensor rows, not LCD overlays.
 * The separate luma row also permits mismatched score and peaking regions. */
typedef struct {
    uint8_t prev_row[C3_FOCUS_W];
    uint32_t sum;
    uint32_t count;
    uint8_t mode, score, settled, skip_next;
} c3_score_t;
extern c3_score_t c3_score_state;
void c3_score_reset(c3_score_t *s);
void c3_score_begin_frame(c3_score_t *s, uint8_t mode);
/* Full sampling on alternate rendered frames unless peaking supplies the
 * score for free; sparse sampling every rendered frame. */
uint8_t c3_score_schedule_frame(c3_score_t *s, uint8_t mode, uint8_t shared);
/* Single exact luma LUT shared by independent score and peak renderer. */
extern const uint16_t c3_luma_hi[256];
extern const uint8_t c3_luma_lo[256];
static inline __attribute__((always_inline)) uint8_t c3_luma_at(const uint8_t *p)
{
    return (uint8_t)((c3_luma_hi[p[0]] + c3_luma_lo[p[1]]) >> 3u);
}
uint8_t c3_score_luma_bytes(uint8_t hi, uint8_t lo);
uint8_t c3_score_row_active(uint8_t mode, uint16_t y);
void c3_score_sample_row(c3_score_t *s, uint16_t y, const uint8_t *raw_row);
void c3_score_end_frame(c3_score_t *s);
void c3_score_end_shared_center(c3_score_t *s, uint32_t roi_sum);
static inline uint8_t c3_score_is_center(uint8_t mode)
{
#if C3_SCORE_FULL_SP_ONLY
    (void)mode;
    return 0u;
#else
    return (uint8_t)(mode == C3_SCORE_CENTER || mode == C3_SCORE_CENTER_SPARSE);
#endif
}
void c3_focus_reset(c3_focus_t *state);
void c3_focus_begin_frame(c3_focus_t *state);
void c3_focus_end_frame(c3_focus_t *state);
/* 4x zoom analyses just 40x30 source samples in the displayed centre ROI.
 * Using their actual count preserves a meaningful normalised score. */
void c3_focus_end_frame_area(c3_focus_t *state, uint32_t roi_sample_count);

/* Returns 1 to paint a red peaking highlight. Always feed the original
 * camera pixel (before monochrome conversion, grid or OSD), so neither the
 * highlight nor previous rows feed back into the gradient estimator.
 * Compiled inline in the OPTIONAL peaking-only preview loop.
 */
static inline __attribute__((always_inline)) uint8_t
c3_focus_sample(c3_focus_t *s, uint16_t x, uint16_t y, uint16_t rgb565,
                uint8_t threshold)
{
    uint8_t r6 = (uint8_t)(((rgb565 >> 11) & 31u) << 1);
    uint8_t g6 = (uint8_t)((rgb565 >> 5) & 63u);
    uint8_t b6 = (uint8_t)((rgb565 & 31u) << 1);
    uint8_t lum = (uint8_t)((2u * r6 + 5u * g6 + b6) >> 3);
    uint8_t up = s->prev_row[x];
    uint8_t left = s->left_luma;
    s->prev_row[x] = lum;
    s->left_luma = lum;
    if (x == 0u || y == 0u) return 0u;
    uint8_t dx = lum >= left ? (uint8_t)(lum - left) : (uint8_t)(left - lum);
    uint8_t dy = lum >= up ? (uint8_t)(lum - up) : (uint8_t)(up - lum);
    uint8_t edge = (uint8_t)(dx + dy); /* max 126, never overflows */
    if (x >= C3_FOCUS_ROI_X0 && x < C3_FOCUS_ROI_X1 &&
        y >= C3_FOCUS_ROI_Y0 && y < C3_FOCUS_ROI_Y1) {
        s->roi_sum += edge;
    }
    /* Exclude dimmed OSD bands. 6-bit luminance > 3 rejects black noise. */
    return (uint8_t)(y >= 24u && y < 216u && lum > 3u && edge >= threshold);
}

/* Four corner brackets identify the region used for score calculation. */
static inline __attribute__((always_inline)) uint8_t
c3_focus_roi_corner(uint16_t x, uint16_t y)
{
    if (y < C3_FOCUS_ROI_Y0 || y >= C3_FOCUS_ROI_Y1 ||
        x < C3_FOCUS_ROI_X0 || x >= C3_FOCUS_ROI_X1) return 0u;
    if ((y == C3_FOCUS_ROI_Y0 || y == C3_FOCUS_ROI_Y1 - 1u) &&
        (x < C3_FOCUS_ROI_X0 + 13u || x >= C3_FOCUS_ROI_X1 - 13u)) return 1u;
    if ((x == C3_FOCUS_ROI_X0 || x == C3_FOCUS_ROI_X1 - 1u) &&
        (y < C3_FOCUS_ROI_Y0 + 13u || y >= C3_FOCUS_ROI_Y1 - 13u)) return 1u;
    return 0u;
}
#endif

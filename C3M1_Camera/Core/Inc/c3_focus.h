/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_FOCUS_H
#define C3_FOCUS_H
#include <stdint.h>
static inline __attribute__((always_inline)) uint8_t c3_green_luma(const uint8_t *p)
{ return (uint8_t)(((p[0]&7u)<<3)|(p[1]>>5)); }

/* Streaming RGB565 gradients. A central ROI adapts the peaking threshold;
 * the displayed contrast score uses a full-area sparse grid. */
enum {
    C3_SCORE_OFF = 0,
    C3_SCORE_CENTER = 1,
    C3_SCORE_CENTER_SPARSE = 2,
    C3_SCORE_FULL = 3,
    C3_SCORE_FULL_SPARSE = 4,
    C3_SCORE_COUNT = 5
};
/* Keep persisted v6 enum values. Any previous valid non-OFF setting is ON. */
static inline uint8_t c3_score_effective_mode(uint8_t mode)
{
    return mode > C3_SCORE_OFF && mode < C3_SCORE_COUNT ? C3_SCORE_FULL_SPARSE : C3_SCORE_OFF;
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

/* Separate score state for use when peaking is disabled. */
typedef struct {
    uint8_t prev_row[C3_FOCUS_W];
    uint32_t sum;
    uint32_t count;
    uint8_t mode, score, settled, skip_next;
} c3_score_t;
extern c3_score_t c3_score_state;
void c3_score_reset(c3_score_t *s);
void c3_score_begin_frame(c3_score_t *s, uint8_t mode);
/* Sparse sampling once per rendered frame. */
uint8_t c3_score_schedule_frame(c3_score_t *s, uint8_t mode, uint8_t shared);
uint8_t c3_score_row_active(uint8_t mode, uint16_t y);
void c3_score_sample_row(c3_score_t *s, uint16_t y, const uint8_t *raw_row);
void c3_score_end_frame(c3_score_t *s);
void c3_focus_reset(c3_focus_t *state);
void c3_focus_begin_frame(c3_focus_t *state);
/* 4x zoom analyses just 40x30 source samples in the displayed centre ROI.
 * Using their actual count preserves a meaningful normalised score. */
void c3_focus_end_frame_area(c3_focus_t *state, uint32_t roi_sample_count);

#endif

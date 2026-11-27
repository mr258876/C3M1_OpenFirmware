/* v0.1.57: central 160x120 full-resolution focus peaking (preview only).
 *
 * The original 320x240 RGB565 row has ALREADY been acquired from FIFO before
 * entry. Keep its pixels everywhere. Compute gradients/highlights only inside
 * the pre-existing score ROI x=[80,240), y=[60,180); no downsampling.
 *
 * To preserve the existing score EXACTLY, pre-load the previous-row luma for
 * ROI columns at y=59, and read the original x=79 pixel for every ROI row's
 * left boundary. Outside the ROI, retain mono/grid/OSD overlay ordering.
 * No change to RGB565, still capture, DMA, sensor clock or SCCB.
 */
#ifndef C3_PREVIEW_PEAK_CENTER_H
#define C3_PREVIEW_PEAK_CENTER_H
#include <stdint.h>
#include "c3_preview_peak_staged.h"

static inline uint8_t c3_center_peak_luma(uint16_t raw)
{
    return (uint8_t)((4u*((raw>>11u)&31u) +
                      5u*((raw>>5u)&63u) + 2u*(raw&31u)) >> 3u);
}

static inline uint8_t c3_center_peak_edge(uint8_t lum, uint8_t left, uint8_t up)
{
#if C3_PEAK_OPT >= 2
    return (uint8_t)(__builtin_abs((int)lum-(int)left) +
                     __builtin_abs((int)lum-(int)up));
#else
    return (uint8_t)((lum >= left ? (uint8_t)(lum-left) : (uint8_t)(left-lum)) +
                     (lum >= up ? (uint8_t)(lum-up) : (uint8_t)(up-lum)));
#endif
}

static inline void c3_effect_peak_line_center(uint8_t *row, uint16_t y,
                                    c3_focus_t *focus, uint8_t threshold,
                                    uint16_t peak_color, uint8_t monochrome,
                                    uint8_t grid, uint8_t score_enabled,
                                    const uint8_t *mask)
{
    const uint8_t is_roi_row = (uint8_t)(y >= C3_FOCUS_ROI_Y0 &&
                                         y < C3_FOCUS_ROI_Y1);
    if (y == C3_FOCUS_ROI_Y0 - 1u) {
        /* Last unmodified row above ROI: supply true vertical neighbors. */
        for (uint16_t x=C3_FOCUS_ROI_X0; x<C3_FOCUS_ROI_X1; ++x)
            focus->prev_row[x] = c3_center_peak_luma(c3_peak_get565(row,x));
    } else if (is_roi_row) {
        /* Preserve the original horizontal gradient at ROI's first column. */
        uint8_t left = c3_center_peak_luma(
            c3_peak_get565(row, C3_FOCUS_ROI_X0 - 1u));
        for (uint16_t x=C3_FOCUS_ROI_X0; x<C3_FOCUS_ROI_X1; ++x) {
            const uint16_t raw = c3_peak_get565(row,x);
            const uint8_t lum = c3_center_peak_luma(raw);
            const uint8_t up = focus->prev_row[x];
            focus->prev_row[x] = lum;
            const uint8_t edge = c3_center_peak_edge(lum,left,up);
            focus->roi_sum += edge;
            left = lum;
            uint16_t color = monochrome ? c3_effect_gray565(raw) : raw;
            if (lum > 3u && edge >= threshold) color = peak_color;
            if (color != raw) c3_peak_put565(row,x,color);
        }
    }

    if (monochrome) {
        /* Outside the ROI no gradient is evaluated; grayscale still covers
         * the ENTIRE frame. Do not gray-out the computed highlight pixels. */
        const uint16_t left_end = is_roi_row ? C3_FOCUS_ROI_X0 : C3_FOCUS_W;
        for (uint16_t x=0u; x<left_end; ++x) {
            const uint16_t c=c3_peak_get565(row,x);
            c3_peak_put565(row,x,c3_effect_gray565(c));
        }
        if (is_roi_row) {
            for (uint16_t x=C3_FOCUS_ROI_X1; x<C3_FOCUS_W; ++x) {
                const uint16_t c=c3_peak_get565(row,x);
                c3_peak_put565(row,x,c3_effect_gray565(c));
            }
        }
    }
    /* Same relative order as D3: peaking -> grid -> corners -> OSD. */
    if (grid) c3_effect_grid_row(row,y,(const uint8_t*)0);
    if (score_enabled) c3_peak_staged_score_corners(row,y);
    if (mask) c3_peak_staged_osd(row,mask);
}
#endif

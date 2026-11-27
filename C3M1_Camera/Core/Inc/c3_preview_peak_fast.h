/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* RGB565 pixel and overlay helpers for streaming peaking. */
#ifndef C3_PREVIEW_PEAK_FAST_H
#define C3_PREVIEW_PEAK_FAST_H
#include "c3_settings.h"
#include "c3_preview_score.h"

static inline __attribute__((always_inline)) unsigned c3_peak_delta(uint8_t a,uint8_t b)
{
    return (unsigned)__builtin_abs((int)a-(int)b);
}
/* Endian/unaligned/alias safe. Cortex-M3 can optimize to a halfword store
 * with REV16 when profitable. No reads/writes to FIFO or LCD MMIO here. */
static inline __attribute__((always_inline)) void c3_peak_fast_put(uint8_t *p,uint16_t c)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t v=__builtin_bswap16(c);
    __builtin_memcpy(p,&v,2);
#else
    p[0]=(uint8_t)(c>>8);p[1]=(uint8_t)c;
#endif
}
static inline void c3_peak_gray_range(uint8_t *row,uint16_t x0,uint16_t x1,uint16_t step)
{
    for(uint16_t x=x0;x<x1;x+=step)
        c3_peak_fast_put(row+2u*x,c3_effect_gray565(c3_effect_get565(row,x)));
}
/* Old peaking order differs from score-only order in the OSD bands:
 * grid -> OSD dim/glyph. Keep pixel output exactly. */
static inline void c3_peak_fast_overlay(uint8_t *row,uint16_t y,uint8_t grid,
                                        const uint8_t *mask)
{
    if(grid)c3_effect_grid_row(row,y,0);

    if(mask)c3_effect_score_line(row,y,0,0,mask);
}
/* For already-dimmed FIFO fast OSD rows: dim(grid(raw)) equals
 * dim(dim(raw)+0x7bef). Glyphs must stay white; there are only 2 grid
 * pixels in the OSD band. Also preserve this for mask==NULL via caller. */
static inline void c3_peak_fast_dimmed_grid(uint8_t *row,const uint8_t *mask)
{
    const uint16_t xs[]={106u,213u};
    for(unsigned i=0;i<2u;++i) {
        uint16_t x=xs[i];
        if(!c3_effect_glyph(mask,x))
            c3_effect_put565(row,x,c3_effect_dim565((uint16_t)(c3_effect_get565(row,x)+0x7befu)));
    }
}
#endif

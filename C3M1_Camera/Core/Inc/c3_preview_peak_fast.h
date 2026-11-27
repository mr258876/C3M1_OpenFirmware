/* v0.1.91: per-frame peaking on original RGB565 rows. No frame cache.
 * Dense output + central adaptive statistic match v190 exactly. Sparse
 * uses a fixed even 2x2 grid and the actual adjacent left/up pixels;
 * the adaptive mean still comes only from the central 160x120 ROI.
 */
#ifndef C3_PREVIEW_PEAK_FAST_H
#define C3_PREVIEW_PEAK_FAST_H
#include "c3_settings.h"
#include "c3_preview_score.h"

static inline uint8_t c3_peak_sparse(uint8_t area)
{
    return (uint8_t)(area==C3_PEAK_AREA_CENTER_SPARSE || area==C3_PEAK_AREA_FULL_SPARSE);
}
static inline uint8_t c3_peak_center(uint8_t area)
{
    return (uint8_t)(area==C3_PEAK_AREA_CENTER || area==C3_PEAK_AREA_CENTER_SPARSE);
}
static inline uint8_t c3_peak_row_active(uint8_t area, uint16_t y)
{
    const uint16_t y0=c3_peak_center(area)?C3_FOCUS_ROI_Y0:24u;
    const uint16_t y1=c3_peak_center(area)?C3_FOCUS_ROI_Y1:216u;
    /* Dense: one reference row and all highlight rows. Sparse: each even
     * row has the preceding odd row as its reference, so all these rows
     * except the final unused odd row are still needed. */
    return (uint8_t)(y>=y0-1u && y<y1-c3_peak_sparse(area));
}
static inline uint8_t c3_peak_score_shared(uint8_t area, uint8_t score_mode)
{
    return (uint8_t)(c3_peak_sparse(area)?score_mode==C3_SCORE_CENTER_SPARSE:score_mode==C3_SCORE_CENTER);
}
static inline uint32_t c3_peak_roi_count(uint8_t area)
{
    return c3_peak_sparse(area)?C3_FOCUS_ROI_AREA/4u:C3_FOCUS_ROI_AREA;
}
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
/* FAST A/B only: green has the same 6-bit scale but different spectral weighting.
 * Never use this approximation for independent focus score. */
static inline __attribute__((always_inline)) uint8_t c3_peak_dense_luma(const uint8_t *p)
{
#if C3_PEAK_EXPERIMENT == 3 || C3_PEAK_EXPERIMENT == 4
    return (uint8_t)(((p[0]&7u)<<3)|(p[1]>>5));
#else
    return c3_luma_at(p);
#endif
}
/* No overlay here: raw score samples must be collected BEFORE this function. */
static inline void c3_peak_fast_pixels(uint8_t *row,uint16_t y,uint8_t area,
                                      c3_focus_t *f,uint8_t threshold,uint16_t color,
                                      uint8_t mono,c3_score_t *shared_sparse)
{
    const uint8_t center=c3_peak_center(area), sparse=c3_peak_sparse(area);
    const uint16_t x0=center?C3_FOCUS_ROI_X0:(sparse?2u:1u);
    const uint16_t x1=center?C3_FOCUS_ROI_X1:C3_FOCUS_W;
    const uint16_t y0=center?C3_FOCUS_ROI_Y0:24u;
    if(!c3_peak_row_active(area,y)) {
        if(mono)c3_peak_gray_range(row,0,C3_FOCUS_W,1);
        return;
    }
    if(y<y0 || (sparse && (y&1u))) {
        if(sparse) {
            for(uint16_t x=x0;x<x1;x+=2u)f->prev_row[x]=c3_luma_at(row+2u*x);
        } else {
            for(uint16_t x=x0;x<x1;++x)f->prev_row[x]=c3_peak_dense_luma(row+2u*x);
        }
        if(mono)c3_peak_gray_range(row,0,C3_FOCUS_W,1);
        return;
    }
    const uint8_t in_roi=(uint8_t)(y>=C3_FOCUS_ROI_Y0 && y<C3_FOCUS_ROI_Y1);
    uint32_t roi_sum=0u;
    if(sparse) {
        /* All sampled x are even, including in FULL SP, so the central
         * subset matches CENTER SP's existing 4x4 coordinates exactly. */
        const uint8_t share=(uint8_t)(shared_sparse && in_roi && (y&3u)==0u);
        uint32_t score_sum=0u;
#define PEAK_SPAN_SP(START,END,ROI,SHARE) do { \
        for(uint16_t x=(START);x<(END);x+=2u) { \
            uint8_t *p=row+2u*x; uint8_t lum=c3_luma_at(p); \
            unsigned edge=c3_peak_delta(lum,c3_luma_at(p-2))+c3_peak_delta(lum,f->prev_row[x]); \
            if(ROI)roi_sum+=edge; \
            if(SHARE && (x&3u)==0u)score_sum+=edge; \
            if(lum>3u && edge>=threshold)c3_peak_fast_put(p,color); \
            else if(mono)c3_peak_fast_put(p,c3_effect_gray565(c3_effect_get565(row,x))); \
        } \
    }while(0)
        if(in_roi) {
            if(!center)PEAK_SPAN_SP(x0,C3_FOCUS_ROI_X0,0,0);
            if(share) { PEAK_SPAN_SP(C3_FOCUS_ROI_X0,C3_FOCUS_ROI_X1,1,1); }
            else { PEAK_SPAN_SP(C3_FOCUS_ROI_X0,C3_FOCUS_ROI_X1,1,0); }
            if(!center)PEAK_SPAN_SP(C3_FOCUS_ROI_X1,x1,0,0);
            if(share) { shared_sparse->sum+=score_sum;shared_sparse->count+=40u; }
        } else { PEAK_SPAN_SP(x0,x1,0,0); }
#undef PEAK_SPAN_SP
        if(mono) {
            c3_peak_gray_range(row,0,x0,1);
            c3_peak_gray_range(row,x0+1u,x1,2);
            c3_peak_gray_range(row,x1,C3_FOCUS_W,1);
        }
    } else {
        uint8_t left=c3_peak_dense_luma(row+2u*(x0-1u));
#if C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
        uint32_t score_sum=0u;
        const uint8_t share=(uint8_t)(shared_sparse && in_roi && (y&3u)==0u);
#define C3_DENSE_SCORE(SHARE) do { if(SHARE && ((p-row)&7u)==0u) score_sum+=edge; }while(0)
#else
#define C3_DENSE_SCORE(SHARE) ((void)0)
#endif
#define PEAK_SPAN(START,END,ROI,SHARE) do { \
        uint8_t *p=row+2u*(START); uint8_t *up=f->prev_row+(START); \
        const uint8_t *end=row+2u*(END); \
        for(;p<end;p+=2,++up) { \
            uint8_t lum=c3_peak_dense_luma(p); \
            unsigned edge=c3_peak_delta(lum,left)+c3_peak_delta(lum,*up); \
            *up=lum;left=lum; \
            if(ROI)roi_sum+=edge; \
            C3_DENSE_SCORE(SHARE); \
            if(lum>3u && edge>=threshold)c3_peak_fast_put(p,color); \
            else if(mono)c3_peak_fast_put(p,c3_effect_gray565((uint16_t)(((uint16_t)p[0]<<8)|p[1]))); \
        } \
    }while(0)
        if(in_roi) {
            if(!center)PEAK_SPAN(x0,C3_FOCUS_ROI_X0,0,0);
#if C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
            if(share){PEAK_SPAN(C3_FOCUS_ROI_X0,C3_FOCUS_ROI_X1,1,1);}
            else
#endif
            {PEAK_SPAN(C3_FOCUS_ROI_X0,C3_FOCUS_ROI_X1,1,0);}
            if(!center)PEAK_SPAN(C3_FOCUS_ROI_X1,x1,0,0);
#if C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
            if(share){shared_sparse->sum+=score_sum;shared_sparse->count+=40u;}
#endif
        } else { PEAK_SPAN(x0,x1,0,0); }
#undef PEAK_SPAN
#undef C3_DENSE_SCORE
        if(mono) {
            c3_peak_gray_range(row,0,x0,1);
            c3_peak_gray_range(row,x1,C3_FOCUS_W,1);
        }
    }
    f->roi_sum+=roi_sum;
}
/* Old peaking order differs from score-only order in the OSD bands:
 * grid -> corners -> OSD dim/glyph. Keep pixel output exactly. */
static inline void c3_peak_fast_overlay(uint8_t *row,uint16_t y,uint8_t grid,
                                        uint8_t score_center,const uint8_t *mask)
{
    if(grid)c3_effect_grid_row(row,y,0);
    if(score_center)c3_effect_score_corners(row,y);
    if(mask)c3_effect_score_line(row,y,0,0,0,mask);
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

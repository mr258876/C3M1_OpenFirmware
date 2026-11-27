/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* Per-frame green-channel 2x2 peaking with 2x2 highlight expansion.
 * Read original neighbors before painting. */
#ifndef C3_PEAKING_H
#define C3_PEAKING_H
#include "c3_preview_peak_fast.h"
/* x is even; store x/2 in one byte. At most 159 full-area hits per row. */
typedef struct { uint8_t hits[160], count, phase, next, last_area; } c3_peak_exp_t;
/* Fixed 2x2 grid. */
#define C3_PEAK_SP_YSTEP 2u
static inline void c3_peak_exp_begin(c3_peak_exp_t *s,uint8_t area,uint8_t enabled)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    s->count=0;
    if(!enabled || s->last_area!=(uint8_t)(area+1u))s->next=0;
    s->last_area=enabled?(uint8_t)(area+1u):0u;
    s->phase=0u;
}
static inline uint8_t c3_peak_exp_sample(uint8_t area,uint16_t y,uint8_t phase)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    const uint16_t y0=24u,y1=216u;
    return y>=y0 && y<y1 && (y&(C3_PEAK_SP_YSTEP-1u))==phase;
}
static inline uint8_t c3_peak_exp_row_active(uint8_t area,uint16_t y,uint8_t phase)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    const uint16_t y1=216u;
    return c3_peak_exp_sample(area,y,phase)||c3_peak_exp_sample(area,y+1u,phase)||
        (y && y<y1 && c3_peak_exp_sample(area,y-1u,phase));
}
static inline uint32_t c3_peak_exp_roi_count(uint8_t area)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    return C3_FOCUS_ROI_AREA/(2u*C3_PEAK_SP_YSTEP);
}
static inline uint8_t c3_peak_exp_shared(uint8_t area,uint8_t score)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    (void)area;
    return c3_score_effective_mode(score)==C3_SCORE_FULL_SPARSE;
}
static inline uint8_t c3_peak_exp_shared_accum(uint8_t area,uint8_t score)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    (void)area;
    return c3_score_effective_mode(score)==C3_SCORE_FULL_SPARSE;
}
static inline void c3_peak_exp_paint(uint8_t *row,const c3_peak_exp_t *s,uint16_t color)
{
    /* 4 bytes = two identical RGB565 pixels, byte-order safe. */
    uint32_t pair=(uint32_t)__builtin_bswap16(color)*0x10001u;
    for(unsigned i=0;i<s->count;++i){
        uint8_t *p=row+4u*s->hits[i];
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        __builtin_memcpy(p,&pair,4);
#else
        (void)pair;c3_peak_fast_put(p,color);c3_peak_fast_put(p+2,color);
#endif
    }
}
static inline __attribute__((always_inline)) uint8_t c3_peak_exp_luma(const uint8_t *p)
{
    return c3_green_luma(p);
}
static inline void c3_peak_exp_pixels(uint8_t *row,uint16_t y,uint8_t area,
                                     c3_focus_t *f,uint8_t threshold,uint16_t color,
                                     uint8_t mono,c3_score_t *shared,c3_peak_exp_t *s)
{
    area=C3_PEAK_AREA_FULL_SPARSE;
    const uint16_t x0=2u,x1=320u,y1=216u;
    const uint8_t sample=c3_peak_exp_sample(area,y,s->phase);
    const uint8_t ref=c3_peak_exp_sample(area,y+1u,s->phase);
    const uint8_t paint=y && y<y1 && c3_peak_exp_sample(area,y-1u,s->phase);
    if(ref){
        /* Capture original luma BEFORE expanding previous row's highlights. */
        for(uint16_t x=x0;x<x1;x+=2u)f->prev_row[x]=c3_peak_exp_luma(row+2u*x);
    }
    if(sample){
        const uint8_t in_roi=(uint8_t)(y>=60u && y<180u);
        const uint8_t share=(uint8_t)(shared && (y&3u)==0u);
        uint32_t sum=0,ss=0;
        s->count=0;
        /* No writes to row until all raw samples and neighbors are consumed. */
#define C3_EXP_SPAN(START,END,ROI,SHARE) do { \
        for(uint16_t x=(START);x<(END);x+=2u){ \
            uint8_t *p=row+2u*x; \
            uint8_t lum=c3_peak_exp_luma(p); \
            unsigned edge=c3_peak_delta(lum,c3_peak_exp_luma(p-2))+c3_peak_delta(lum,f->prev_row[x]); \
            if(ROI)sum+=edge; \
            if(SHARE && (x&3u)==0u)ss+=edge; \
            if(lum>3u && edge>=threshold)s->hits[s->count++]=(uint8_t)(x>>1); \
        } \
    }while(0)
        /* No second luma/gradient pass. Only score rows execute the extra
         * accumulation, outside the adaptive ROI as well as inside it. */
#define C3_EXP_FULL_ROW(SHARE) do { \
        if(in_roi){ \
            C3_EXP_SPAN(x0,80u,0,SHARE); \
            C3_EXP_SPAN(80u,240u,1,SHARE); \
            C3_EXP_SPAN(240u,x1,0,SHARE); \
        }else{C3_EXP_SPAN(x0,x1,0,SHARE);} \
    }while(0)
        if(share){C3_EXP_FULL_ROW(1);shared->sum+=ss;shared->count+=C3_SCORE_SP_ROW_COUNT;}
        else{C3_EXP_FULL_ROW(0);}
#undef C3_EXP_FULL_ROW
#undef C3_EXP_SPAN
        f->roi_sum+=sum;
    }
    if(mono)c3_peak_gray_range(row,0,C3_FOCUS_W,1);
    if(sample || paint)c3_peak_exp_paint(row,s,color);
    if(paint)s->count=0;
}
#endif

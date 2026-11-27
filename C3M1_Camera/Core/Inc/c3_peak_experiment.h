/* v0.1.92: per-frame sparse sampling with local 2x2 highlight expansion.
 * No preceding-frame mask; never feed painted pixels back to gradients. */
#ifndef C3_PEAK_EXPERIMENT_H
#define C3_PEAK_EXPERIMENT_H
#include "c3_preview_peak_fast.h"
#ifndef C3_PEAK_EXPERIMENT
#define C3_PEAK_EXPERIMENT 0
#endif
#if C3_PEAK_EXPERIMENT < 0 || C3_PEAK_EXPERIMENT > 4
#error invalid peak experiment
#endif
/* Only the CENTER SP highlight rectangle changes; FS and adaptive threshold
 * continue to use the original 160x120 ROI and fixed sample phase. */
#ifndef C3_PEAK_SP_CENTER_W
#define C3_PEAK_SP_CENTER_W 160
#endif
#if C3_PEAK_SP_CENTER_W != 160 && C3_PEAK_SP_CENTER_W != 224 && C3_PEAK_SP_CENTER_W != 256
#error invalid sparse center width
#endif
#if C3_PEAK_SP_CENTER_W != 160 && (C3_PEAK_EXPERIMENT != 4 || !C3_FOCUS_GREEN)
#error expanded sparse center requires GREEN FAST/SP22
#endif
#define C3_PEAK_SP_CENTER_H (C3_PEAK_SP_CENTER_W*3u/4u)
#define C3_PEAK_SP_X0 ((C3_FOCUS_W-C3_PEAK_SP_CENTER_W)/2u)
#define C3_PEAK_SP_X1 (C3_PEAK_SP_X0+C3_PEAK_SP_CENTER_W)
#define C3_PEAK_SP_Y0 ((C3_FOCUS_HEIGHT-C3_PEAK_SP_CENTER_H)/2u)
#define C3_PEAK_SP_Y1 (C3_PEAK_SP_Y0+C3_PEAK_SP_CENTER_H)
#if C3_PEAK_FULL_SP_ONLY && (C3_PEAK_EXPERIMENT != 4 || !C3_FOCUS_GREEN)
#error full sparse only requires GREEN SP22
#endif
#if C3_SCORE_FULL_SP_ONLY && !C3_PEAK_FULL_SP_ONLY
#error unified score requires FULL SP-only peak
#endif
#if C3_PEAK_EXPERIMENT
/* x is even; store x/2 in one byte. At most 159 full-area hits per row. */
typedef struct { uint8_t hits[160], count, phase, next, last_area; } c3_peak_exp_t;
/* v193 mode 4 = validated FAST dense + SP22 fixed sparse. */
#define C3_PEAK_SP_FIXED (C3_PEAK_EXPERIMENT==1 || C3_PEAK_EXPERIMENT==4)
#define C3_PEAK_SP_YSTEP (C3_PEAK_SP_FIXED?2u:4u)
static inline void c3_peak_exp_begin(c3_peak_exp_t *s,uint8_t area,uint8_t enabled)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
    s->count=0;
    if(!enabled || s->last_area!=(uint8_t)(area+1u))s->next=0;
    s->last_area=enabled?(uint8_t)(area+1u):0u;
    /* Four phases cover every row, rather than permanently missing odd rows.
     * 0,2,1,3 separates the first two fields by half the row pitch. */
    s->phase=C3_PEAK_SP_FIXED?0u:(uint8_t)(((s->next&1u)<<1)|((s->next&2u)>>1));
    if(enabled && c3_peak_sparse(area) && !C3_PEAK_SP_FIXED)s->next=(uint8_t)((s->next+1u)&3u);
}
static inline uint8_t c3_peak_exp_sample(uint8_t area,uint16_t y,uint8_t phase)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
    const uint16_t y0=c3_peak_center(area)?C3_PEAK_SP_Y0:24u,y1=c3_peak_center(area)?C3_PEAK_SP_Y1:216u;
    return y>=y0 && y<y1 && (y&(C3_PEAK_SP_YSTEP-1u))==phase;
}
static inline uint8_t c3_peak_exp_row_active(uint8_t area,uint16_t y,uint8_t phase)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
    if(!c3_peak_sparse(area))return c3_peak_row_active(area,y);
    const uint16_t y1=c3_peak_center(area)?C3_PEAK_SP_Y1:216u;
    return c3_peak_exp_sample(area,y,phase)||c3_peak_exp_sample(area,y+1u,phase)||
        (y && y<y1 && c3_peak_exp_sample(area,y-1u,phase));
}
static inline uint32_t c3_peak_exp_roi_count(uint8_t area)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
    return c3_peak_sparse(area)?C3_FOCUS_ROI_AREA/(2u*C3_PEAK_SP_YSTEP):C3_FOCUS_ROI_AREA;
}
static inline uint8_t c3_peak_exp_shared(uint8_t area,uint8_t score)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
#if C3_SCORE_FULL_SP_ONLY
    (void)area;
    return c3_score_effective_mode(score)==C3_SCORE_FULL_SPARSE;
#elif C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
    if(!c3_peak_sparse(area))return score==C3_SCORE_CENTER || score==C3_SCORE_CENTER_SPARSE;
#else
    /* The independent score's luma and fixed grid must not change. */
    if(!c3_peak_sparse(area))return C3_PEAK_EXPERIMENT!=3 && C3_PEAK_EXPERIMENT!=4 && score==C3_SCORE_CENTER;
#endif
    return C3_PEAK_SP_FIXED && score==C3_SCORE_CENTER_SPARSE;
}
static inline uint8_t c3_peak_exp_shared_accum(uint8_t area,uint8_t score)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
#if C3_SCORE_FULL_SP_ONLY
    (void)area;
    return c3_score_effective_mode(score)==C3_SCORE_FULL_SPARSE;
#elif C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
    (void)area;
    return score==C3_SCORE_CENTER_SPARSE;
#else
    (void)score;
    return c3_peak_sparse(area);
#endif
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
#if C3_FOCUS_GREEN && C3_PEAK_EXPERIMENT==4
    return c3_green_luma(p);
#else
    return c3_luma_at(p);
#endif
}
static inline void c3_peak_exp_pixels(uint8_t *row,uint16_t y,uint8_t area,
                                     c3_focus_t *f,uint8_t threshold,uint16_t color,
                                     uint8_t mono,c3_score_t *shared,c3_peak_exp_t *s)
{
#if C3_PEAK_FULL_SP_ONLY
    area=C3_PEAK_AREA_FULL_SPARSE;
#endif
#if !C3_PEAK_FULL_SP_ONLY
    if(!c3_peak_sparse(area)){
        c3_peak_fast_pixels(row,y,area,f,threshold,color,mono,shared);
        return;
    }
#endif
    const uint8_t center=c3_peak_center(area);
    const uint16_t x0=center?C3_PEAK_SP_X0:2u,x1=center?C3_PEAK_SP_X1:320u,y1=center?C3_PEAK_SP_Y1:216u;
    const uint8_t sample=c3_peak_exp_sample(area,y,s->phase);
    const uint8_t ref=c3_peak_exp_sample(area,y+1u,s->phase);
    const uint8_t paint=y && y<y1 && c3_peak_exp_sample(area,y-1u,s->phase);
    if(ref){
        /* Capture original luma BEFORE expanding previous row's highlights. */
        for(uint16_t x=x0;x<x1;x+=2u)f->prev_row[x]=c3_peak_exp_luma(row+2u*x);
    }
    if(sample){
        const uint8_t in_roi=(uint8_t)(y>=60u && y<180u);
#if C3_SCORE_FULL_SP_ONLY
        const uint8_t share=(uint8_t)(shared && (y&3u)==0u);
#else
        const uint8_t share=(uint8_t)(shared && in_roi && (y&3u)==0u);
#endif
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
#if C3_SCORE_FULL_SP_ONLY
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
#else
        if(in_roi){
            if(C3_PEAK_SP_CENTER_W==160 ? !center : x0<80u)C3_EXP_SPAN(x0,80u,0,0);
            if(share){C3_EXP_SPAN(80u,240u,1,1);}
            else{C3_EXP_SPAN(80u,240u,1,0);}
            if(C3_PEAK_SP_CENTER_W==160 ? !center : x1>240u)C3_EXP_SPAN(240u,x1,0,0);
        }else{C3_EXP_SPAN(x0,x1,0,0);}
        if(share){shared->sum+=ss;shared->count+=40u;}
#endif
#undef C3_EXP_SPAN
        f->roi_sum+=sum;
    }
    if(mono)c3_peak_gray_range(row,0,C3_FOCUS_W,1);
    if(sample || paint)c3_peak_exp_paint(row,s,color);
    if(paint)s->count=0;
}
#endif
#endif

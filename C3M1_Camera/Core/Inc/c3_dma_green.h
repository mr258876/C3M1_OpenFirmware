#ifndef C3_DMA_GREEN_H
#define C3_DMA_GREEN_H
#include "c3_preview_dma_render.h"
/* Cached raw green for dense gradient rows only. Original CPU fallback unchanged. */
static inline uint8_t c3_dma_green_needed(const c3_live_render_t *p,uint16_t y,uint8_t phase)
{
    return (uint8_t)((p->peaking&&!c3_peak_sparse(p->peak_area)&&c3_peak_exp_row_active(p->peak_area,y,phase))||
       (p->score_sample&&!p->score_shared&&(p->score_mode==C3_SCORE_CENTER||p->score_mode==C3_SCORE_FULL)&&c3_score_row_active(p->score_mode,y)));
}
static inline void c3_dma_green_score(c3_score_t *s,uint16_t y,const uint8_t *g)
{
    const uint8_t center=c3_score_is_center(s->mode),sp=s->mode==C3_SCORE_CENTER_SPARSE||s->mode==C3_SCORE_FULL_SPARSE;
    const unsigned x0=center?80u:1u,x1=center?240u:320u,y0=center?60u:1u;
    unsigned sum=0;
    if(!c3_score_row_active(s->mode,y))return;
    if(sp){
        if(((y-(y0-1u))&3u)==0u){for(unsigned x=x0;x<x1;x+=4u)s->prev_row[x]=g[x];return;}
        for(unsigned x=x0;x<x1;x+=4u)sum+=c3_peak_delta(g[x],g[x-1])+c3_peak_delta(g[x],s->prev_row[x]);
        s->count+=(x1-x0+3u)/4u;
    }else{
        if(y<y0){__builtin_memcpy(s->prev_row+x0,g+x0,x1-x0);return;}
        uint8_t left=g[x0-1];
        for(unsigned x=x0;x<x1;++x){uint8_t lum=g[x];sum+=c3_peak_delta(lum,left)+c3_peak_delta(lum,s->prev_row[x]);s->prev_row[x]=lum;left=lum;}
        s->count+=x1-x0;
    }
    s->sum+=sum;
}
static inline void c3_dma_green_peak(uint8_t *row,const uint8_t *g,uint16_t y,uint8_t area,c3_focus_t *f,
        uint8_t threshold,uint16_t color,uint8_t mono,c3_score_t *shared,c3_peak_exp_t *s)
{
    const uint8_t center=c3_peak_center(area),sp=c3_peak_sparse(area);
    if(!c3_peak_exp_row_active(area,y,s->phase)){if(mono)c3_peak_gray_range(row,0,320,1);return;}
    const unsigned x0=center?(sp?C3_PEAK_SP_X0:80u):(sp?2u:1u),x1=center?(sp?C3_PEAK_SP_X1:240u):320u;
    const uint8_t in_roi=y>=60u&&y<180u,share=shared&&in_roi&&(y&3u)==0u;
    unsigned sum=0,ss=0;
    if(sp){
        const unsigned y1=center?C3_PEAK_SP_Y1:216u;
        const uint8_t sample=c3_peak_exp_sample(area,y,s->phase),ref=c3_peak_exp_sample(area,y+1,s->phase),paint=y&&y<y1&&c3_peak_exp_sample(area,y-1,s->phase);
        if(ref)for(unsigned x=x0;x<x1;x+=2u)f->prev_row[x]=g[x];
        if(sample){
            s->count=0;
#define C3_CACHED_SPAN(A,B,ROI,SHARE) do {for(unsigned x=(A);x<(B);x+=2u){ \
    uint8_t lum=g[x];unsigned edge=c3_peak_delta(lum,g[x-1])+c3_peak_delta(lum,f->prev_row[x]); \
    if(ROI){sum+=edge;}if((SHARE)&&(x&3u)==0u){ss+=edge;} \
    if(lum>3u&&edge>=threshold)s->hits[s->count++]=(uint8_t)(x>>1);}}while(0)
            if(in_roi){if(x0<80u)C3_CACHED_SPAN(x0,80u,0,0);if(share){C3_CACHED_SPAN(80u,240u,1,1);}else{C3_CACHED_SPAN(80u,240u,1,0);}if(x1>240u)C3_CACHED_SPAN(240u,x1,0,0);}
            else{C3_CACHED_SPAN(x0,x1,0,0);}
#undef C3_CACHED_SPAN
            f->roi_sum+=sum;if(share){shared->sum+=ss;shared->count+=40u;}
        }
        if(mono)c3_peak_gray_range(row,0,320,1);
        if(sample||paint)c3_peak_exp_paint(row,s,color);
        if(paint)s->count=0;
    }else{
        unsigned y0=center?60u:24u;
        if(y<y0){__builtin_memcpy(f->prev_row+x0,g+x0,x1-x0);if(mono)c3_peak_gray_range(row,0,320,1);return;}
        uint8_t left=g[x0-1];
#define C3_CACHED_DENSE(A,B,ROI,SHARE) do {for(unsigned x=(A);x<(B);++x){ \
    uint8_t lum=g[x];unsigned edge=c3_peak_delta(lum,left)+c3_peak_delta(lum,f->prev_row[x]); \
    f->prev_row[x]=lum;left=lum;if(ROI){sum+=edge;}if((SHARE)&&(x&3u)==0u){ss+=edge;} \
    if(lum>3u&&edge>=threshold)c3_peak_fast_put(row+2u*x,color); \
    else if(mono)c3_peak_fast_put(row+2u*x,c3_effect_gray565(c3_effect_get565(row,x)));}}while(0)
        if(in_roi){if(x0<80u)C3_CACHED_DENSE(x0,80u,0,0);if(share){C3_CACHED_DENSE(80u,240u,1,1);}else{C3_CACHED_DENSE(80u,240u,1,0);}if(x1>240u)C3_CACHED_DENSE(240u,x1,0,0);}
        else{C3_CACHED_DENSE(x0,x1,0,0);}
#undef C3_CACHED_DENSE
        if(mono){c3_peak_gray_range(row,0,x0,1);c3_peak_gray_range(row,x1,320,1);}
        f->roi_sum+=sum;if(share){shared->sum+=ss;shared->count+=40u;}
    }
}
static inline void c3_dma_green_render(uint8_t *line,const uint8_t *g,uint16_t y,const uint8_t *mask,
    const c3_live_render_t *p,c3_focus_t *f,c3_score_t *s,c3_peak_exp_t *exp)
{
    if(p->peaking){
        if(p->score_sample&&!p->score_shared&&c3_score_row_active(p->score_mode,y)){
            C3_LP_BEGIN();c3_dma_green_score(s,y,g);C3_LP_ADD(score);
        }
        C3_LP_BEGIN();
        c3_dma_green_peak(line,g,y,p->peak_area,f,p->threshold,p->color,p->mono,
            p->score_shared&&c3_peak_exp_shared_accum(p->peak_area,p->score_mode)?s:0,exp);
        C3_LP_ADD(peak);
        c3_peak_fast_overlay(line,y,p->grid,p->score_center,mask);
    }else{
        if(p->score_sample&&c3_score_row_active(p->score_mode,y)){
            C3_LP_BEGIN();c3_dma_green_score(s,y,g);C3_LP_ADD(score);
        }
        c3_effect_score_line(line,y,p->mono,p->grid,p->score_center,mask);
    }
}
#endif

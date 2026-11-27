#ifndef C3_PREVIEW_DMA_RENDER_H
#define C3_PREVIEW_DMA_RENDER_H
#include "c3_peaking.h"
/* Original RowOpt2 output after DMA unpack. Input must be the untouched
 * RGB565 MSB/LSB row. Same distinct OSD/grid ordering for active peak,
 * inactive peak and score-only as c3_camera.c. No MMIO in this function. */
typedef struct {
    uint8_t grid,mono,peaking,score_sample,score_shared,score_mode,peak_area,threshold;
    uint16_t color;
} c3_live_render_t;
static inline void c3_live_render_row(uint8_t *line,uint16_t y,const uint8_t *mask,
    const c3_live_render_t *p,c3_focus_t *f,c3_score_t *s,c3_peak_exp_t *exp)
{
    if(p->peaking) {
        uint8_t peak_row=c3_peak_exp_row_active(p->peak_area,y,exp->phase);
        uint8_t score_row=(uint8_t)(p->score_sample&&!p->score_shared&&c3_score_row_active(p->score_mode,y));
        if(!peak_row&&!score_row&&!p->mono) {
            if(mask) {
                c3_effect_score_line(line,y,0,0,mask);
                if(p->grid)c3_peak_fast_dimmed_grid(line,mask);
            } else if(p->grid)c3_effect_grid_row(line,y,0);

        } else {
            if(score_row){

                c3_score_sample_row(s,y,line);

            }

            c3_peak_exp_pixels(line,y,p->peak_area,f,p->threshold,p->color,p->mono,
                p->score_shared&&c3_peak_exp_shared_accum(p->peak_area,p->score_mode)?s:0,exp);

            c3_peak_fast_overlay(line,y,p->grid,mask);
        }
    } else {
        if(p->score_sample&&c3_score_row_active(p->score_mode,y)){

            c3_score_sample_row(s,y,line);

        }
        c3_effect_score_line(line,y,p->mono,p->grid,mask);
    }
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define C3_ROW_FIFO_BYTE() fifo_mock_read()
#define C3_ROW_DIM565(v) c3_effect_dim565(v)
#include "c3_preview_effect_opt.h"
static uint8_t row_samples[640];
static unsigned cursor;
static uint8_t fifo_mock_read(void) { return row_samples[cursor++]; }
#include "c3_preview_row_opt.h"
#undef C3_ROW_FIFO_BYTE
#undef C3_ROW_DIM565

/* A literal implementation of v0.1.40's LCD DMA fall-back branch. */
static void reference_line(uint8_t *line, uint16_t y, c3_focus_t *focus,
                           uint8_t peaking, uint8_t grid, uint8_t mono,
                           uint8_t score, uint16_t peak_color,
                           uint8_t threshold, const uint8_t *mask)
{
    const uint8_t grid_y = (uint8_t)(grid && (y==80u || y==160u));
    for (uint16_t x=0;x<320;x++) {
        uint16_t raw = ((uint16_t)row_samples[2*x] << 8) | row_samples[2*x+1u];
        uint16_t color;
        if (peaking) {
            uint8_t edge = c3_focus_sample(focus,x,y,raw,threshold);
            color = mono ? c3_effect_gray565(raw) : raw;
            if (edge) color=peak_color;
            if (grid && (grid_y || x==106u || x==213u)) color = c3_effect_grid565(color);
            if (score && c3_focus_roi_corner(x,y)) color=0x07E0u;
            if (mask) { color=c3_effect_dim565(color); if(c3_effect_glyph(mask,x)) color=0xFFFFu; }
        } else if (mask) {
            color = mono ? c3_effect_gray565(raw) : raw;
            color = c3_effect_dim565(color);
            if (grid && (x==106u || x==213u)) color=c3_effect_grid565(color);
            if (c3_effect_glyph(mask,x)) color=0xFFFFu;
        } else if (grid && !mono) {
            color = (grid_y || x==106u || x==213u) ? c3_effect_grid565(raw) : raw;
        } else {
            color = mono ? c3_effect_gray565(raw) : raw;
            if (grid && (grid_y || x==106u || x==213u)) color=c3_effect_grid565(color);
        }
        c3_effect_put565(line,x,color);
    }
}
static unsigned rng_state=0x9e3779b9u;
static unsigned rand32(void) {rng_state^=rng_state<<13; rng_state^=rng_state>>17; rng_state^=rng_state<<5;return rng_state;}
static void die(const char *msg,int run,int y,int x) {fprintf(stderr,"FAILED %s run=%d y=%d byte=%d\n",msg,run,y,x); exit(1);}
int main(void)
{
    const unsigned tests=4;
    unsigned rows=0;
    for(unsigned cfg=0;cfg<16;cfg++) {
        const uint8_t peak = (cfg & 1u)!=0, grid = (cfg & 2u)!=0;
        const uint8_t mono = (cfg & 4u)!=0, score = (cfg & 8u)!=0;
        for(unsigned run=0;run<tests;run++) {
            c3_focus_t old_state,new_state;
            c3_focus_reset(&old_state); c3_focus_reset(&new_state);
            for(unsigned i=0;i<320;i++) old_state.prev_row[i]=new_state.prev_row[i]=(uint8_t)rand32();
            c3_focus_begin_frame(&old_state); c3_focus_begin_frame(&new_state);
            for(uint16_t y=0;y<240;y++) {
                for(unsigned i=0;i<640;i++) row_samples[i]=(uint8_t)rand32();
                uint8_t mask[40];
                for(unsigned i=0;i<40;i++) mask[i]=(uint8_t)rand32();
                const uint8_t *osd=((run&1u)==0u && (y<24u || y>=216u))?mask:NULL;
                struct {uint8_t before[16]; uint8_t data[640];uint8_t after[16];} opt;
                memset(&opt,0xA5,sizeof(opt));
                uint8_t ref[640];
                uint16_t peak_color[] = {0xF800u,0xFFE0u,0x07FFu,0x07E0u};
                uint16_t col=peak_color[run&3u];
                uint8_t threshold=(uint8_t)(10u+(rand32()%96));
                reference_line(ref,y,&old_state,peak,grid,mono,score,col,threshold,osd);
                cursor=0;
                if(peak) {
                    c3_row_copy_plain(opt.data);
                    c3_effect_peak_line(opt.data,y,&new_state,threshold,col,mono,grid,score,osd);
                } else if(grid && !mono) {
                    if(osd) c3_row_copy_osd(opt.data,osd);
                    else c3_row_copy_plain(opt.data);
                    c3_effect_grid_row(opt.data,y,osd);
                } else {
                    /* Baseline path to ensure the reference itself is sound. */
                    if(osd&&!grid&&!mono) c3_row_copy_osd(opt.data,osd);
                    else if(!grid&&!mono) c3_row_copy_plain(opt.data);
                    else {
                        /* Legacy general per-pixel fall-back for other modes. */
                        memcpy(opt.data,ref,640); cursor=640;
                    }
                }
                if(cursor!=640) die("FIFO count",cfg*tests+run,y,cursor);
                if(memcmp(opt.data,ref,640)) {
                    for(int k=0;k<640;k++) if(opt.data[k]!=ref[k]) die("pixel mismatch",cfg*tests+run,y,k);
                }
                for(unsigned i=0;i<16;i++) if(opt.before[i]!=0xA5||opt.after[i]!=0xA5) die("memory boundary",cfg*tests+run,y,i);
                if(memcmp(&old_state,&new_state,sizeof(old_state))) die("focus state",cfg*tests+run,y,0);
                rows++;
            }
            if(peak){c3_focus_end_frame(&old_state); c3_focus_end_frame(&new_state);
                if(memcmp(&old_state,&new_state,sizeof(old_state))) die("focus final state",cfg*tests+run,240,0);
            }
        }
    }
    printf("PASS %u rows, %u pixels, exact grid/peak/OSD and focus-state output\n",rows,rows*320u);
    return 0;
}

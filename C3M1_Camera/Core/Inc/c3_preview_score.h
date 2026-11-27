#ifndef C3_PREVIEW_SCORE_H
#define C3_PREVIEW_SCORE_H
#include "c3_preview_effect_opt.h"

static inline void c3_effect_score_corners(uint8_t *row, uint16_t y)
{
    if (y==C3_FOCUS_ROI_Y0 || y==C3_FOCUS_ROI_Y1-1u) {
        for (uint16_t x=C3_FOCUS_ROI_X0;x<C3_FOCUS_ROI_X0+13u;++x)
            c3_effect_put565(row,x,0x07E0u);
        for (uint16_t x=C3_FOCUS_ROI_X1-13u;x<C3_FOCUS_ROI_X1;++x)
            c3_effect_put565(row,x,0x07E0u);
    } else if ((y>C3_FOCUS_ROI_Y0 && y<C3_FOCUS_ROI_Y0+13u) ||
               (y>=C3_FOCUS_ROI_Y1-13u && y<C3_FOCUS_ROI_Y1-1u)) {
        c3_effect_put565(row,C3_FOCUS_ROI_X0,0x07E0u);
        c3_effect_put565(row,C3_FOCUS_ROI_X1-1u,0x07E0u);
    }
}
/* Same ordering as ordinary score-OFF preview, including grid intersections
 * in the OSD bands. Process an OSD mask byte at once; empty groups
 * avoid per-pixel mask extraction. Corners are never inside the OSD bands. */
static inline void c3_effect_score_line(uint8_t *row, uint16_t y,
                                       uint8_t monochrome, uint8_t grid,
                                       uint8_t score_center, const uint8_t *mask)
{
    if (monochrome) {
        for (uint16_t x=0u;x<C3_FOCUS_W;++x)
            c3_effect_put565(row,x,c3_effect_gray565(c3_effect_get565(row,x)));
    }
    if (mask) {
        uint8_t *p=row;
        for (uint16_t m=0u;m<C3_FOCUS_W/8u;++m) {
            uint8_t bits=mask[m];
            if (!bits) {
                for (uint8_t b=0;b<8u;++b,p+=2) {
                    uint16_t c=(uint16_t)(((uint16_t)p[0]<<8)|p[1]);
                    c=c3_effect_dim565(c);
                    p[0]=(uint8_t)(c>>8);p[1]=(uint8_t)c;
                }
            } else {
                for (uint8_t b=0;b<8u;++b,p+=2,bits<<=1) {
                    uint16_t c=(uint16_t)(((uint16_t)p[0]<<8)|p[1]);
                    c=(bits&0x80u)?0xffffu:c3_effect_dim565(c);
                    p[0]=(uint8_t)(c>>8);p[1]=(uint8_t)c;
                }
            }
        }
    }
    if (grid) c3_effect_grid_row(row,y,mask);
    if (score_center) c3_effect_score_corners(row,y);
}
#endif

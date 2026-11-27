/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_PREVIEW_SCORE_H
#define C3_PREVIEW_SCORE_H
#include "c3_preview_effect_opt.h"

/* Same ordering as ordinary score-OFF preview, including grid intersections
 * in the OSD bands. Process an OSD mask byte at once; empty groups
 * avoid per-pixel mask extraction.  */
static inline void c3_effect_score_line(uint8_t *row, uint16_t y,
                                       uint8_t monochrome, uint8_t grid,
                                       const uint8_t *mask)
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

}
#endif

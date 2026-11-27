/* v0.1.42 experimental, preview-only exact full-resolution peaking.
 *
 * Read the FIFO to a complete RGB565 row BEFORE calling this function.
 * All gradient/ROI state is updated once per pixel, in the original raster
 * order. Separate passes avoid per-pixel checks for rare grid/ROI/OSD art.
 * No I/O / no extra frame buffer / no change to still-capture code.
 *
 * Old reference: c3_effect_peak_line() in c3_preview_effect_opt.h.
 */
#ifndef C3_PREVIEW_PEAK_STAGED_H
#define C3_PREVIEW_PEAK_STAGED_H
#include <stdint.h>
#include "c3_preview_effect_opt.h"

/* v0.1.44 preview-only performance A/B switches.
 * 0: original v0.1.42 D3 exact pixel and gradient operations (mainline).
 * 1: endian-safe 16-bit RGB565 accesses through builtin memcpy + REV16.
 * 2: 1 plus simplified int32 absolute-gradient arithmetic.
 * The LCD DMA line buffer is 4-byte aligned; memcpy also permits odd test
 * addresses without aliasing or undefined unaligned uint16_t accesses.
 */
#ifndef C3_PEAK_OPT
#define C3_PEAK_OPT 0
#endif
#if C3_PEAK_OPT > 2
#error "C3_PEAK_OPT must be 0, 1, or 2"
#endif
#if C3_PEAK_OPT >= 1
static inline uint16_t c3_peak_get565(const uint8_t *row, uint16_t x)
{
    uint16_t native;
    __builtin_memcpy(&native, row + (uint32_t)x * 2u, sizeof(native));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(native);
#else
    return native;
#endif
}
static inline void c3_peak_put565(uint8_t *row, uint16_t x, uint16_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const uint16_t native = __builtin_bswap16(value);
#else
    const uint16_t native = value;
#endif
    __builtin_memcpy(row + (uint32_t)x * 2u, &native, sizeof(native));
}
#else
#define c3_peak_get565 c3_effect_get565
#define c3_peak_put565 c3_effect_put565
#endif

static inline void c3_peak_staged_score_corners(uint8_t *row, uint16_t y)
{
    const uint16_t green = 0x07E0u;
    if (y == C3_FOCUS_ROI_Y0 || y == (C3_FOCUS_ROI_Y1 - 1u)) {
        for (uint16_t x=C3_FOCUS_ROI_X0; x<C3_FOCUS_ROI_X0 + 13u; ++x)
            c3_peak_put565(row, x, green);
        for (uint16_t x=C3_FOCUS_ROI_X1 - 13u; x<C3_FOCUS_ROI_X1; ++x)
            c3_peak_put565(row, x, green);
    } else if ((y > C3_FOCUS_ROI_Y0 && y < C3_FOCUS_ROI_Y0 + 13u) ||
               (y >= C3_FOCUS_ROI_Y1 - 13u && y < C3_FOCUS_ROI_Y1 - 1u)) {
        c3_peak_put565(row, C3_FOCUS_ROI_X0, green);
        c3_peak_put565(row, C3_FOCUS_ROI_X1 - 1u, green);
    }
}

static inline void c3_peak_staged_osd(uint8_t *row, const uint8_t *mask)
{
    /* Exactly the final two reference operations: dim then glyph-to-white. */
    for (uint16_t x=0u; x<C3_FOCUS_W; ++x) {
        uint16_t c=c3_effect_dim565(c3_peak_get565(row,x));
        if ((mask[x >> 3u] & (0x80u >> (x & 7u))) != 0u) c=0xFFFFu;
        c3_peak_put565(row,x,c);
    }
}

/*
 * The hot region is y=24..215. Hoist y-boundaries out of the pixel loop;
 * split the focus ROI into three x-ranges so no per-pixel bounds check is
 * needed for the score accumulation. For the 48 OSD-band rows, there is no
 * need to calculate gradients: neither peaking nor focus ROI is active.
 */
static inline void c3_effect_peak_line_staged(uint8_t *row, uint16_t y,
                                   c3_focus_t *focus, uint8_t threshold,
                                   uint16_t peak_color, uint8_t monochrome,
                                   uint8_t grid, uint8_t score_enabled,
                                   const uint8_t *mask)
{
    uint8_t left;
#define C3_PEAK_LUMA(V) ((uint8_t)((4u*(((V)>>11u)&31u) + 5u*(((V)>>5u)&63u) + 2u*((V)&31u))>>3u))
    /* x=0 must update prev_row and left_luma but never contribute a gradient. */
    const uint16_t p0=c3_peak_get565(row,0u);
    left=C3_PEAK_LUMA(p0);
    focus->prev_row[0]=left;
    if(monochrome) c3_peak_put565(row,0u,c3_effect_gray565(p0));

#define C3_PEAK_LUMA_ONLY() do { \
        const uint16_t raw=c3_peak_get565(row,x); \
        const uint8_t lum=C3_PEAK_LUMA(raw); \
        focus->prev_row[x]=lum; \
        left=lum; \
        if(monochrome) c3_peak_put565(row,x,c3_effect_gray565(raw)); \
    }while(0)

#if C3_PEAK_OPT >= 2
#define C3_PEAK_EDGE(L,LEFT,UP) ((uint8_t)( __builtin_abs((int)(L)-(int)(LEFT)) + __builtin_abs((int)(L)-(int)(UP)) ))
#else
#define C3_PEAK_EDGE(L,LEFT,UP) ((uint8_t)( ((L)>=(LEFT)?(uint8_t)((L)-(LEFT)):(uint8_t)((LEFT)-(L))) + ((L)>=(UP)?(uint8_t)((L)-(UP)):(uint8_t)((UP)-(L))) ))
#endif
#define C3_PEAK_GRADIENT(ADD_ROI) do { \
        const uint16_t raw=c3_peak_get565(row,x); \
        const uint8_t lum=C3_PEAK_LUMA(raw); \
        const uint8_t up=focus->prev_row[x]; \
        focus->prev_row[x]=lum; \
        const uint8_t edge=C3_PEAK_EDGE(lum,left,up); \
        if(ADD_ROI) focus->roi_sum+=edge; \
        left=lum; \
        uint16_t color=monochrome?c3_effect_gray565(raw):raw; \
        if(lum>3u && edge>=threshold) color=peak_color; \
        if(color!=raw) c3_peak_put565(row,x,color); \
    }while(0)

    if (y < 24u || y >= 216u) {
        /* OSD bands contain no peak pixels or focus-score ROI. */
        for(uint16_t x=1u; x<C3_FOCUS_W; ++x) C3_PEAK_LUMA_ONLY();
    } else if (y>=C3_FOCUS_ROI_Y0 && y<C3_FOCUS_ROI_Y1) {
        for(uint16_t x=1u; x<C3_FOCUS_ROI_X0; ++x) C3_PEAK_GRADIENT(0);
        for(uint16_t x=C3_FOCUS_ROI_X0; x<C3_FOCUS_ROI_X1; ++x) C3_PEAK_GRADIENT(1);
        for(uint16_t x=C3_FOCUS_ROI_X1; x<C3_FOCUS_W; ++x) C3_PEAK_GRADIENT(0);
    } else {
        for(uint16_t x=1u; x<C3_FOCUS_W; ++x) C3_PEAK_GRADIENT(0);
    }
    focus->left_luma=left;
#undef C3_PEAK_GRADIENT
#undef C3_PEAK_EDGE
#undef C3_PEAK_LUMA_ONLY
#undef C3_PEAK_LUMA
    /* Keep the exact reference order: edge -> grid -> score corners -> OSD. */
    if(grid) c3_effect_grid_row(row,y,(const uint8_t *)0);
    if(score_enabled) c3_peak_staged_score_corners(row,y);
    if(mask) c3_peak_staged_osd(row,mask);
}
#endif

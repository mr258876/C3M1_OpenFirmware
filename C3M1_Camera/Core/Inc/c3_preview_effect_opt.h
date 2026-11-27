/* v0.1.41 preview-only CPU overlay experiments.
 * Assumes dst contains one 320-pixel RGB565 row in MSB-first LCD byte order.
 * No FIFO / RCLK accesses and NO extra framebuffer allocations.
 * Behaviour is pixel-identical to the original c3_camera.c composition order.
 */
#ifndef C3_PREVIEW_EFFECT_OPT_H
#define C3_PREVIEW_EFFECT_OPT_H
#include <stdint.h>
#include "c3_focus.h"

static inline uint16_t c3_effect_get565(const uint8_t *row, uint16_t x)
{
    uint32_t i = (uint32_t)x * 2u;
    return (uint16_t)((uint16_t)row[i] << 8 | row[i+1u]);
}
static inline void c3_effect_put565(uint8_t *row, uint16_t x, uint16_t c)
{
    uint32_t i = (uint32_t)x * 2u;
    row[i] = (uint8_t)(c >> 8);
    row[i+1u] = (uint8_t)c;
}
static inline uint16_t c3_effect_dim565(uint16_t c)
{
    return (uint16_t)((c & 0xF7DEu) >> 1);
}
static inline uint16_t c3_effect_grid565(uint16_t c)
{
    return (uint16_t)(c3_effect_dim565(c) + 0x7BEFu);
}
static inline uint16_t c3_effect_gray565(uint16_t c)
{
    uint16_t r = (c >> 11) & 31u;
    uint16_t g = (c >> 5) & 63u;
    uint16_t b = c & 31u;
    uint16_t y5 = (uint16_t)((r * 3u + (g >> 1) * 6u + b) / 10u);
    if (y5 > 31u) y5 = 31u;
    return (uint16_t)((y5 << 11) | (y5 << 6) | y5);
}
static inline uint8_t c3_effect_glyph(const uint8_t *mask, uint16_t x)
{
    return (uint8_t)(mask != (const uint8_t *)0 &&
        (mask[x >> 3] & (uint8_t)(0x80u >> (x & 7u))) != 0u);
}
static inline void c3_effect_patch_grid(uint8_t *row, uint16_t x,
                                        const uint8_t *mask)
{
    /* The original shader overlays white glyphs AFTER the grid. */
    if (!c3_effect_glyph(mask, x))
        c3_effect_put565(row, x, c3_effect_grid565(c3_effect_get565(row, x)));
}
static inline void c3_effect_grid_row(uint8_t *row, uint16_t y,
                                       const uint8_t *mask)
{
    if (y == 80u || y == 160u) {
        for (uint16_t x = 0u; x < 320u; ++x)
            c3_effect_patch_grid(row, x, mask);
    } else {
        c3_effect_patch_grid(row, 106u, mask);
        c3_effect_patch_grid(row, 213u, mask);
    }
}
#endif

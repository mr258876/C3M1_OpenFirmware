/* v0.1.38 diagnostic-only buffer compositing. Never linked in mainline.
 * The RGB565 bytes and mask convention are identical to RowOpt2. */
#ifndef C3_ROW_PROFILE_MIX_H
#define C3_ROW_PROFILE_MIX_H
#include <stdint.h>
#ifndef C3_ROW_DIM565
#error "C3_ROW_DIM565(v) must be defined"
#endif
static inline __attribute__((always_inline)) void
c3_row_profile_mix_osd(uint8_t *dst, const uint8_t *src, const uint8_t *mask)
{
    for (uint16_t m = 0u; m < 40u; ++m) {
        uint8_t glyphs = mask[m];
        if (glyphs == 0u) {
            for (uint8_t bit = 0u; bit < 8u; ++bit) {
                uint16_t raw = (uint16_t)((uint16_t)(*src++) << 8);
                raw |= *src++;
                uint16_t color = C3_ROW_DIM565(raw);
                *dst++ = (uint8_t)(color >> 8);
                *dst++ = (uint8_t)color;
            }
        } else {
            for (uint8_t bit = 0u; bit < 8u; ++bit) {
                uint16_t raw = (uint16_t)((uint16_t)(*src++) << 8);
                raw |= *src++;
                uint16_t color = (glyphs & 0x80u) ? 0xFFFFu : C3_ROW_DIM565(raw);
                glyphs = (uint8_t)(glyphs << 1);
                *dst++ = (uint8_t)(color >> 8);
                *dst++ = (uint8_t)color;
            }
        }
    }
}
#endif

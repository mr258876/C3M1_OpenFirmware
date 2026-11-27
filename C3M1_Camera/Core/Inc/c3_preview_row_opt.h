/* v0.1.37 CPU preview-only ROW experiment, compiled out when disabled.
 * Include after defining C3_ROW_FIFO_BYTE() and C3_ROW_DIM565(v).
 * The same implementations are compiled in the host equivalence test.
 */
#ifndef C3_PREVIEW_ROW_OPT_H
#define C3_PREVIEW_ROW_OPT_H
#include <stdint.h>
#ifndef C3_ROW_FIFO_BYTE
#error "C3_ROW_FIFO_BYTE() must be defined before including this header"
#endif
#ifndef C3_ROW_DIM565
#error "C3_ROW_DIM565(v) must be defined before including this header"
#endif
#define C3_ROW_WIDTH 320u
#define C3_ROW_BYTES (C3_ROW_WIDTH * 2u)

/* v0.1.39: optional little-endian halfword writes to the 4-byte-aligned
 * LCD DMA row buffers. may_alias is needed because their declared type is
 * uint8_t[]; a normal uint16_t* cast violates strict aliasing rules.
 * This switch changes SRAM stores only, never the GPIO FIFO read primitive. */
#ifndef C3_ROW_PACK16
#define C3_ROW_PACK16 0
#endif
#if C3_ROW_PACK16 != 0 && C3_ROW_PACK16 != 1
#error "C3_ROW_PACK16 must be 0 or 1"
#endif
#if C3_ROW_PACK16
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Packed RGB565 LCD DMA bytes require a little-endian CPU"
#endif
typedef uint16_t __attribute__((may_alias)) c3_row_alias_u16_t;
/* line_buf[][] in c3_lcd_dma.c is 4-byte-aligned; every row is 640 B. */
#endif

/* v193 opt-in ASM reader is defined only in the ARM preview TU.
 * OSD fused read and all historical fallback paths remain unchanged. */
#ifndef C3_FIFO_ROW_ARM
#define C3_FIFO_ROW_ARM 0
#endif

/* Two FIFO samples/pixel, same byte order and GPIO sequence as B0. */
static inline __attribute__((always_inline)) void c3_row_copy_plain(uint8_t *dst)
{
#if C3_FIFO_ROW_ARM && (defined(__arm__) || defined(__thumb__))
    c3_fifo_lowreg_row(dst);
#elif C3_ROW_PACK16
    /* In SRAM, [hi,lo] bytes correspond to 0xLOHI on a little-endian M3. */
    c3_row_alias_u16_t *out = (c3_row_alias_u16_t *)(void *)dst;
    for (uint16_t x = 0u; x < C3_ROW_WIDTH; ++x) {
        uint16_t hi = C3_ROW_FIFO_BYTE();
        uint16_t lo = C3_ROW_FIFO_BYTE();
        *out++ = (uint16_t)(hi | (lo << 8));
    }
#else
    for (uint16_t x = 0u; x < C3_ROW_WIDTH; ++x) {
        *dst++ = C3_ROW_FIFO_BYTE();
        *dst++ = C3_ROW_FIFO_BYTE();
    }
#endif
}

/* Exactly the v0.1.36 visual behavior for unmodified RGB565 with OSD
 * (no grid, monochrome or peaking).  The 8-bit text mask is one byte per
 * eight pixels, MSB first. RGB565 background is dimmed exactly as before.
 */
static inline __attribute__((always_inline)) void c3_row_copy_osd(uint8_t *dst,
                                                                    const uint8_t *mask)
{
#if C3_ROW_PACK16
    c3_row_alias_u16_t *out = (c3_row_alias_u16_t *)(void *)dst;
#define C3_STORE_RGB565(col) do { \
    *out++ = __builtin_bswap16((uint16_t)(col)); \
} while (0)
#else
#define C3_STORE_RGB565(col) do { \
    uint16_t c3_tmp_color = (uint16_t)(col); \
    *dst++ = (uint8_t)(c3_tmp_color >> 8); \
    *dst++ = (uint8_t)c3_tmp_color; \
} while (0)
#endif
    for (uint16_t m = 0u; m < C3_ROW_WIDTH / 8u; ++m) {
        uint8_t glyphs = mask[m];
        if (glyphs == 0u) {
            for (uint8_t bit = 0u; bit < 8u; ++bit) {
                uint16_t hi = C3_ROW_FIFO_BYTE();
                uint16_t raw = (uint16_t)((hi << 8) | C3_ROW_FIFO_BYTE());
                uint16_t color = C3_ROW_DIM565(raw);
                C3_STORE_RGB565(color);
            }
        } else {
            for (uint8_t bit = 0u; bit < 8u; ++bit) {
                uint16_t hi = C3_ROW_FIFO_BYTE();
                uint16_t raw = (uint16_t)((hi << 8) | C3_ROW_FIFO_BYTE());
                uint16_t color = (glyphs & 0x80u) ? 0xFFFFu : C3_ROW_DIM565(raw);
                glyphs = (uint8_t)(glyphs << 1);
                C3_STORE_RGB565(color);
            }
        }
    }
#undef C3_STORE_RGB565
}
#endif

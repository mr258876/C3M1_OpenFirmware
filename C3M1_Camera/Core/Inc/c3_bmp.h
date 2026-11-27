#ifndef C3_BMP_H
#define C3_BMP_H
#include <stdint.h>
/* 24-bit top-down BI_RGB, no row padding needed at width 320.
 * A negative DIB height allows forward-only FIFO streaming. */
#define C3_BMP_WIDTH 320u
#define C3_BMP_HEIGHT 240u
#define C3_BMP_HEADER_BYTES 54u
#define C3_BMP_ROW_BYTES (C3_BMP_WIDTH * 3u)
#define C3_BMP_IMAGE_BYTES (C3_BMP_ROW_BYTES * C3_BMP_HEIGHT)
#define C3_BMP_FILE_BYTES (C3_BMP_HEADER_BYTES + C3_BMP_IMAGE_BYTES)
void c3_bmp_make_header(uint8_t out[C3_BMP_HEADER_BYTES]);
/* Convert RGB565 as displayed into BGR888 for an ordinary 24-bit BMP. */
void c3_bmp_pixel_bgr(uint16_t pixel, uint8_t out[3]);
#endif

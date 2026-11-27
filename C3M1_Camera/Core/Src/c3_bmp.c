#include "c3_bmp.h"
#include <string.h>
static void le16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void le32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
void c3_bmp_make_header(uint8_t out[C3_BMP_HEADER_BYTES])
{
    memset(out,0,C3_BMP_HEADER_BYTES);
    out[0]='B'; out[1]='M';
    le32(out+2,C3_BMP_FILE_BYTES);
    le32(out+10,C3_BMP_HEADER_BYTES);
    le32(out+14,40u);          /* BITMAPINFOHEADER */
    le32(out+18,C3_BMP_WIDTH);
    le32(out+22,(uint32_t)(-(int32_t)C3_BMP_HEIGHT)); /* top-down */
    le16(out+26,1u);           /* planes */
    le16(out+28,24u);          /* BGR888 */
    le32(out+34,C3_BMP_IMAGE_BYTES);
    le32(out+38,2835u);       /* 72 dpi, nominal */
    le32(out+42,2835u);
}
void c3_bmp_pixel_bgr(uint16_t pixel,uint8_t out[3])
{
    uint8_t b = (uint8_t)(pixel & 31u);
    uint8_t g = (uint8_t)((pixel >> 5) & 63u);
    uint8_t r = (uint8_t)((pixel >> 11) & 31u);
    out[0]=(uint8_t)((b<<3)|(b>>2));
    out[1]=(uint8_t)((g<<2)|(g>>4));
    out[2]=(uint8_t)((r<<3)|(r>>2));
}

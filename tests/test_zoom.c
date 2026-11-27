#include "c3_zoom.h"
#include <assert.h>
#include <stdio.h>

/* OV7725 produces one *native* pixel for each LCD pixel; the 4X label
 * means four times image AREA (half the field of view in each axis).
 * Verify register geometry, mode selection, no software enlargement,
 * and unmodified per-frame FIFO byte count. */
int main(void)
{
    c3_zoom_window_t full = c3_zoom_window(0);
    c3_zoom_window_t crop = c3_zoom_window(1);
    assert(C3_ZOOM_VGA_W == 640u && C3_ZOOM_VGA_H == 480u);
    assert(C3_ZOOM_OUT_W == 320u && C3_ZOOM_OUT_H == 240u);
    assert(C3_ZOOM_FRAME_BYTES == 153600u);
    assert(full.com7 == 0x46u);    /* QVGA RGB565, current full-FoV path */
    assert(full.hstart == 0x3Fu && full.vstrt == 0x03u);
    assert(crop.com7 == 0x06u);    /* VGA RGB565, 1:1 native crop */
    assert(crop.hstart == 0x4Eu);  /* VGA 0x26 + 160 pixels / 4 */
    assert(crop.vstrt == 0x43u);   /* VGA 0x07 + 120 rows / 2 */
    assert(full.hsize == 0x50u && crop.hsize == 0x50u);
    assert(full.vsize == 0x78u && crop.vsize == 0x78u);
    assert(full.houtsize == 0x50u && crop.houtsize == 0x50u);
    assert(full.voutsize == 0x78u && crop.voutsize == 0x78u);
    assert(full.href == 0u && crop.href == 0u);
    /* Boundaries of the centre 320x240 crop in sensor pixel coordinates. */
    assert((C3_ZOOM_VGA_W - C3_ZOOM_OUT_W) / 2u == 160u);
    assert((C3_ZOOM_VGA_H - C3_ZOOM_OUT_H) / 2u == 120u);
    assert(160u + 319u < 640u && 120u + 239u < 480u);
    assert(c3_zoom_window(2).com7 == 0x06u);
    puts("Zoom sensor window verified: QVGA full -> VGA centre 320x240 native, no interpolation");
    return 0;
}

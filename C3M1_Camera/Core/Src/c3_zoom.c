/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_zoom.h"

c3_zoom_window_t c3_zoom_window(uint8_t zoom4x)
{
    c3_zoom_window_t win;
    win.com7 = zoom4x ? 0x06u : 0x46u;  /* VGA / QVGA, RGB565 */
    win.hstart = zoom4x ?
        (uint8_t)(0x26u + ((C3_ZOOM_VGA_W - C3_ZOOM_OUT_W) / 2u) / 4u) :
        0x3Fu;
    win.vstrt = zoom4x ?
        (uint8_t)(0x07u + ((C3_ZOOM_VGA_H - C3_ZOOM_OUT_H) / 2u) / 2u) :
        0x03u;
    win.hsize = (uint8_t)(C3_ZOOM_OUT_W >> 2);
    win.vsize = (uint8_t)(C3_ZOOM_OUT_H >> 1);
    win.houtsize = win.hsize;
    win.voutsize = win.vsize;
    win.href = 0u; /* All window origins and sizes are aligned. */
    return win;
}

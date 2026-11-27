/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_ZOOM_H
#define C3_ZOOM_H
#include <stdint.h>

/* OV7725 output is 320x240 in both modes. "4X" is 4x image AREA
 * / 2x linear field-of-view crop: centre 320x240 of VGA 640x480.
 * There is no software resampling; this is not 4x linear zoom. */
#define C3_ZOOM_OUT_W 320u
#define C3_ZOOM_OUT_H 240u
#define C3_ZOOM_VGA_W 640u
#define C3_ZOOM_VGA_H 480u
#define C3_ZOOM_FRAME_BYTES (C3_ZOOM_OUT_W * C3_ZOOM_OUT_H * 2u)

typedef struct {
    uint8_t com7;
    uint8_t hstart;
    uint8_t hsize;
    uint8_t vstrt;
    uint8_t vsize;
    uint8_t href;
    uint8_t houtsize;
    uint8_t voutsize;
} c3_zoom_window_t;

/* Pure configuration map: QVGA full FoV or native VGA centre crop.
 * The VGA start constants follow the OV7725 Rev 1.4 data sheet.
 * Real board must confirm sensor HREF alignment and mirror orientation. */
c3_zoom_window_t c3_zoom_window(uint8_t zoom4x);
#endif

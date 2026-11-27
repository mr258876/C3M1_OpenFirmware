/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_FPS_H
#define C3_FPS_H
#include <stdint.h>
/* Count COMPLETED, displayed preview frames over ~1 second of wall time,
 * including camera capture wait, FIFO read, LCD GRAM transfer and OSD.
 * This measures real preview throughput, not OV7725's sensor frame rate.
 */
typedef struct {
    uint32_t start_ms;
    uint16_t frames;
    uint16_t tenths;    /* 0.0 .. 99.9 */
    uint8_t started;
} c3_fps_t;
void c3_fps_reset(c3_fps_t *s);
/* Returns 1 when a new value becomes available. Rollover-safe tick math. */
uint8_t c3_fps_frame(c3_fps_t *s, uint32_t now_ms);
#endif

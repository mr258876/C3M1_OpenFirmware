/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_FIFO_DMA_PLAN_H
#define C3_FIFO_DMA_PLAN_H
#include <stdint.h>
#define C3_FD_ROWS 240u
#define C3_FD_BYTES 640u
typedef struct { uint8_t mhz, ticks, high_ticks, sample_tick; } c3_fd_plan_t;
/* DMA transfer error codes. */
enum { C3_FD_OK=0, C3_FD_DMA_ERROR=1, C3_FD_TIMEOUT=2,
       C3_FD_COUNT=3, C3_FD_STOP=4, C3_FD_NDTR=5,
       C3_FD_PIN=6, C3_FD_CLOCK=9 };
#endif

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_FRAME_SCHED_H
#define C3_FRAME_SCHED_H

#include <stdint.h>

/* All fields are shared between the VSYNC IRQ and foreground. Foreground
 * changes must be made in a short interrupt-masked critical section.
 * 384 KiB FIFO holds TWO complete 320x240 RGB565 frames (307200 bytes)
 * plus 86016 bytes of headroom; a third full frame would exceed capacity.
 */
#define C3_FIFO_BYTES          393216u
#define C3_PREVIEW_FRAME_BYTES (320u * 240u * 2u)
#define C3_FIFO_GUARD_BYTES    128u
#if (2u * C3_PREVIEW_FRAME_BYTES + C3_FIFO_GUARD_BYTES) > C3_FIFO_BYTES
#error Two preview frames no longer fit in the AL422B with the read/write guard.
#endif

typedef enum {
    C3_SCHED_OFF = 0,
    C3_SCHED_PREVIEW = 1, /* continuous producer, <=2 frames outstanding */
    C3_SCHED_STILL = 2    /* one-shot producer, never overlap RAW read/write */
} c3_sched_mode_t;

typedef struct {
    volatile uint8_t mode;
    volatile uint8_t armed;
    volatile uint8_t writing;
    volatile uint8_t ready;
    volatile uint8_t reading;
    volatile uint32_t edges;
    volatile uint32_t frames_completed;
    volatile uint32_t frames_skipped;
} c3_frame_sched_t;

void c3_sched_reset(c3_frame_sched_t *s, c3_sched_mode_t mode);
/* Called from EXTI at the start of every frame. Return 1 to enable FIFO WEN.
 * The output value is the state to DRIVE, not a pulse or an action mask. */
uint8_t c3_sched_vsync(c3_frame_sched_t *s);
/* Only while IRQ is masked: requires a subsequent frame already in the
 * FIFO, or a new producer in progress, to respect FIFO read-behind-write. */
uint8_t c3_sched_acquire(c3_frame_sched_t *s);
/* Only while IRQ is masked; release a completely consumed preview frame. */
void c3_sched_release(c3_frame_sched_t *s);
/* Only while IRQ is masked; arm a fresh single-frame snapshot. */
void c3_sched_arm_still(c3_frame_sched_t *s);

#endif

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_CAMERA_H
#define C3_CAMERA_H
#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "c3_settings.h"
#include "c3_meter.h"
/* AL422B preview ring schedule. The producer is VSYNC EXTI, the consumer
 * is the foreground full-screen renderer. RAW still uses a one-shot mode. */
/* Set the selected sensor preview mode; persists across RAW still capture. */
/* 0=OK; 1=sensor SCCB/init; 2=window setup; 3=exposure apply. */
extern volatile uint32_t c3_camera_last_init_stage;
/* Post-init checks, readable via SWD when a failure occurs. */
extern volatile uint32_t c3_camera_pid_good_of_5;
extern volatile uint32_t c3_camera_pid_last;
extern volatile uint32_t c3_camera_vsync_edges;
extern volatile uint32_t c3_camera_vsync_last_ms;
extern volatile uint32_t c3_camera_last_sensor_code;
extern volatile uint32_t c3_camera_last_still_code;
extern volatile uint32_t c3_camera_last_still_stage;
void c3_camera_preview_select_zoom(uint8_t zoom4x);
uint8_t c3_camera_preview_init(void);
/* Reset FIFO frame boundaries after SCCB exposure changes, keeping sensor
 * mode intact. Prevents the previous/in-flight frame from being displayed. */
void c3_camera_preview_rearm(void);
/* Stop FIFO producer before drawing a static settings screen. */
void c3_camera_preview_stop(void);
/* Wait for two fresh VSYNC edges while FIFO scheduler is OFF. */
uint8_t c3_camera_wait_zoom_frame(void);
uint8_t c3_camera_raw_init(void);
/* Bench-only no-reset RAW switch, with automatic full-init fallback. */
uint8_t c3_camera_raw_fast_init(void);
extern volatile uint32_t c3_camera_raw_fast_path_used;
extern volatile uint32_t c3_camera_raw_fast_fallbacks;
/* Fail-closed RAW8 -> RGB565; on success it keeps the sensor's live AWB
 * registers and AEC/GAIN under caller control, and does NOT arm FIFO. */
uint8_t c3_camera_rgb_fast_restore(void);
/* Capture one fresh full-FOV 320x240 RGB565 still for BMP output. */
uint8_t c3_camera_bmp_init(void);
/* v0.1.62 experimental 1X BMP shutter path: keep live RGB565 sensor/AEC/AGC,
 * change ONLY the AL422B frame scheduler into single-frame mode. The caller
 * must verify the sensor is already in full-FOV 1X preview mode. */
uint8_t c3_camera_bmp_fast_init(void);
/* Wait for a whole VSYNC-bounded frame, then lock the FIFO for reading.
   Returns 0 on timeout; keeps write disabled on all exits. */
uint8_t c3_camera_capture(uint32_t timeout_ms);
/* Skip N COMPLETE sensor frame periods after a fresh VSYNC, with FIFO WEN
 * disabled. Only valid in idle STILL mode, before the final capture.
 * timeout_ms is the maximum gap between observed edges. On timeout stop
 * the scheduler; the caller must not capture/save and must recover preview. */
uint8_t c3_camera_wait_still_frames(unsigned frames, uint32_t timeout_ms);

uint8_t c3_camera_preview_acquire(uint32_t timeout_ms);
void c3_camera_preview_release(void);
void c3_camera_vsync_isr(void);
void c3_camera_fifo_begin(void);
/* Freeze one completed RAW image; only a save commit or explicit discard
 * may enable FIFO writes again. Rewind resets both AL422B addresses. */
uint8_t c3_camera_raw_hold(void);
uint8_t c3_camera_raw_rewind(void);
void c3_camera_raw_read_end(void);
void c3_camera_raw_discard(void);
/* Register-only FIFO byte read. Preserve order RCLK-low -> sample -> high;
 * AL422B read timing is a board-level validation item, do not reorder.
 * Used both by preview and RAW8 storage to avoid per-byte BL calls.
 */
static inline __attribute__((always_inline)) uint8_t c3_camera_fifo_read_fast(void)
{
    GPIOB->BRR = GPIO_PIN_4;
    uint8_t pixel = (uint8_t)((GPIOG->IDR >> 1) & 0xFFu);
    GPIOB->BSRR = GPIO_PIN_4;
    return pixel;
}
/* Compatibility symbol, not used in the high-frequency loops. */
uint8_t c3_camera_fifo_read(void);
/* UI is combined with pixels in one full-screen GRAM write. */
void c3_camera_preview_draw(const c3_settings_t *settings, uint8_t zoom4x,
                            uint8_t sd_ok,
                            uint32_t next_number, uint16_t fps_tenths,
                            uint8_t shoot_mode, uint8_t shutter_code,
                            uint8_t gain_step, uint8_t raw_dial_code,
                            int8_t ev_half_steps, uint8_t ev_active,
                            const c3_meter_t *meter);
#endif

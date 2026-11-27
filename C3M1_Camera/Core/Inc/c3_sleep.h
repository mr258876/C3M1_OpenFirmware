/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_SLEEP_H
#define C3_SLEEP_H
#include <stdint.h>
/* 0=OFF, 1=15s, 2=30s, 3=1 min, 4=3 min */
uint32_t c3_sleep_timeout_ms(uint8_t timer_choice);
uint8_t c3_sleep_due(uint8_t timer_choice,uint32_t now_ms,
                     uint32_t last_activity_ms,uint16_t pressed);
/* A visible but nonblocking tail after the last completed SD sector write.
 * This affects LED only; no artificial delay is added to disk operations. */
#define C3_SD_LED_HOLD_MS 180u
uint8_t c3_sd_led_should_light(uint8_t write_in_progress, uint8_t recent_write,
                               uint32_t now_ms, uint32_t last_write_ms);
/* Smooth breathing ~5 seconds, 250 Hz timer; returns LED on-time in us. */
uint16_t c3_sleep_breath_us(uint32_t elapsed_ms);
/* PD9 open-drain. All functions below are only called in full mainline. */
void c3_sleep_led_start(void);
void c3_sleep_led_update(uint32_t elapsed_ms);
void c3_sleep_led_stop(void);
void c3_sleep_led_irq(void);
/* PD9 awake mode: OFF at idle, ON for actual FatFs SD sector writes. */
void c3_sd_led_write_begin(void);
void c3_sd_led_write_end(void);
void c3_sd_led_poll(uint32_t now_ms);
#endif

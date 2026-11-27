/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_METER_H
#define C3_METER_H
#include <stdint.h>

/* OV7725 read-back from preview AEC/AGC registers. It reports SENSOR
 * EXPOSURE ROWS and ANALOG GAIN, not calibrated seconds or ISO sensitivity.
 * Neither quantity is guaranteed to match a separately reset RAW session.
 */
#define C3_METER_SAMPLE_MS 350u

typedef uint8_t (*c3_meter_read_t)(uint8_t reg, uint8_t *value);
typedef struct {
    uint16_t exposure_rows;
    uint16_t gain_x16;          /* fixed point: 16 = 1.00x, 40 = 2.50x */
    uint32_t last_attempt_ms;
    uint8_t valid;
    uint8_t attempted;
    /* Exposure-limit warning is independent of the live AEC/GAIN label. */
    uint16_t warning_rows;
    uint8_t warning_fine, warning_candidate, warning_samples, warning;
    /* Raw readbacks for the diagnostic build. No change to gain decoding. */
    uint8_t raw_gain, yave, read_aeb, read_aew, read_com8, read_com9;
    uint8_t warning_reason, read_flags;
} c3_meter_t;

void c3_meter_reset(c3_meter_t *m);
/* Return 1 if a fresh result (including an invalid result) was recorded.
 * Bound the work to <= 2 AECH-low-AECH attempts + one GAIN read.
 * Only call between FIFO->LCD frame transfers, never in VSYNC ISR. */
uint8_t c3_meter_poll(c3_meter_t *m, uint32_t now_ms, c3_meter_read_t read_reg);
uint16_t c3_meter_decode_gain_x16(uint8_t gain_reg);
#define C3_METER_WARN_NONE 0u
#define C3_METER_WARN_DARK 1u
#define C3_METER_WARN_BRIGHT 2u
#define C3_METER_WARN_SAMPLES 4u
#define C3_METER_WARN_MARGIN 4u
#define C3_METER_GAIN_NEAR_MAX 248u /* 0x7F = 15.5x */
#define C3_METER_GAIN_NEAR_MIN 17u  /* 0x01 = 1.0625x */
#define C3_METER_REASON_NORMAL 0u
#define C3_METER_REASON_SAMPLE 1u
#define C3_METER_REASON_GAIN 2u
#define C3_METER_REASON_CONFIG 3u
#define C3_METER_REASON_SETTLING 4u
#define C3_METER_REASON_ACTIVE 5u
#define C3_METER_REASON_FINE 6u
/* Called once after each fresh c3_meter_poll (including invalid samples).
 * P=0, S=1, M=2. Read YAVE, optionally LAEC at P's bright limit.
 * Near-limit gain includes the adjacent coarse/fine encoding (15.5x),
 * and readings above 16x when relevant sensor controls are verified.
 * P dark: near-max gain + an AEC plateau, NOT a guessed night-mode row cap.
 * Verify live COM8/COM9/AEB/AEW before accepting a warning candidate. */
void c3_meter_update_warning(c3_meter_t *m, uint8_t shoot_mode,
                             uint8_t aeb, uint8_t aew,
                             c3_meter_read_t read_reg);
/* Text fits the existing 5x7, 320-pixel OSD. Appended 'A' means the sensor
 * AUTO algorithm owns that quantity, not that the displayed number is ISO. */
void c3_meter_exposure_label(char out[12], uint16_t rows, uint8_t is_auto);
/* Modelled live shutter time. row_ns uses EXHCL-corrected RGB line period.
 * The conversion is a theory-backed estimate, not an exposure-meter result.
 * Auto status is already visible as P on the top OSD, so only 1/x is printed.
 * Unknown or implausible AEC -> 1/---; >=1s -> x.xS. */
void c3_meter_shutter_label(char out[12], uint16_t rows, uint32_t row_ns);
void c3_meter_gain_label(char out[12], uint16_t gain_x16, uint8_t is_auto);
#endif

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_SHUTTER_TIMING_H
#define C3_SHUTTER_TIMING_H
#include <stdint.h>
/* v177 fixed-clock models verified in read-only v174-v176 field experiments.
 * Times are MODEL estimates at 12 MHz PCLK, NOT photodiode measurements. */
typedef enum { C3_TIMING_RGB1X=0, C3_TIMING_RGB4X=1,
               C3_TIMING_RAW8=2 } c3_timing_format_t;
typedef struct {
    uint16_t rows;        /* complete AEC rows; 1..8191 */
    uint16_t dummy;       /* EXHCH[7:4]:EXHCL[7:0], 0..4095 */
    uint8_t clkrc;        /* 0x00: normal, 0x40: bypass (4X 1/8000) */
} c3_shutter_plan_t;
/* dial 0..8; 0xFF for auto. manual is 1 for selected shutter. */
uint8_t c3_shutter_plan(c3_timing_format_t fmt, uint8_t dial,
                        uint8_t manual, c3_shutter_plan_t *plan);
uint32_t c3_shutter_row_ns(c3_timing_format_t fmt,
                           const c3_shutter_plan_t *plan);
uint32_t c3_shutter_exposure_ns(c3_timing_format_t fmt,
                                const c3_shutter_plan_t *plan);
#endif

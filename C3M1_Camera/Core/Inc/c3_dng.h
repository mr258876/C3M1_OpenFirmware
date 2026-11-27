/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_DNG_H
#define C3_DNG_H
#include <stdint.h>

/* OV7725 and AL422B still deliver 644 bytes per row.  Column 642 is a
 * permanent zero in actual camera samples; column 643 is nonzero but is
 * discarded with the invalid column to preserve even Bayer phase.
 * Do not change the sensor's 644-byte row timing/HSIZE just for DNG cropping.
 * Crop EACH row when streaming from FIFO into DNG, never compact the entire
 * FIFO stream as a single linear byte sequence.
 */
#define C3_RAW_SENSOR_WIDTH 644u
#define C3_RAW_WIDTH 642u
#define C3_RAW_HEIGHT 480u
#define C3_RAW_SENSOR_BYTES (C3_RAW_SENSOR_WIDTH * C3_RAW_HEIGHT)
#define C3_RAW_BYTES (C3_RAW_WIDTH * C3_RAW_HEIGHT)
_Static_assert(C3_RAW_SENSOR_WIDTH == C3_RAW_WIDTH + 2u,
               "Only last two RAW bytes of each sensor row are discarded");
_Static_assert((C3_RAW_WIDTH % 2u) == 0u,
               "RAW output width must preserve a 2x2 CFA phase");
#define C3_DNG_HEADER_BYTES 512u
/* RAW8 sensor samples with a scalar DNG black level and embedded profile. */
#define C3_DNG_SAMPLE_BYTES 1u
#define C3_DNG_SCALE 1u
#define C3_DNG_STRIP_BYTES (C3_RAW_BYTES * C3_DNG_SAMPLE_BYTES)
#define C3_DNG_TAIL_BYTES 168u

/* Fills a complete 512-byte, little-endian TIFF/DNG prefix.
 * Pixel byte 0 must follow immediately after this prefix.
 * CFA describes STORED RAW BGGR (not the displayed, 180-degree-rotated image).
 * Experimental ColorMatrix1 and ForwardMatrix1 are derived from the 67.5%
 * visual matrix at 5250K. AsShotNeutral may be recorded per exposure.
 */
/* Fallback WB (NOT destructive gains): 1.045:1:0.533 under the 5250K
 * reference LED. Used when sensor AWB readback is invalid or unavailable.
 * User's chart reflectances and the lamp SPD have not been measured. */
#ifndef C3_DNG_NEUTRAL_R_PERMILLE
#define C3_DNG_NEUTRAL_R_PERMILLE 1045u
#endif
#ifndef C3_DNG_NEUTRAL_G_PERMILLE
#define C3_DNG_NEUTRAL_G_PERMILLE 1000u
#endif
#ifndef C3_DNG_NEUTRAL_B_PERMILLE
#define C3_DNG_NEUTRAL_B_PERMILLE 533u
#endif

/* v0.1.65: snapshot AWB BEFORE OV7725 RAW initialization resets preview AWB.
 * Checked sensor gain registers: RED=0x02, GREEN=0x03, BLUE=0x01.
 * To correct user-observed green/magenta polarity in Camera Raw, invert the
 * common-mode R/B neutral component about the 5250K fallback reference,
 * keeping the R/B (first-order temperature) ratio constant. This is an
 * experimental sign correction, not an absolute tint calibration.
 * No white-balance multiplication or matrix transformation touches RAW bytes.
 */
typedef uint8_t (*c3_dng_reg_reader_t)(uint8_t reg, uint8_t *out);
void c3_dng_begin_shot(void);   /* set 5250K fallback for a NEW capture */
uint8_t c3_dng_capture_preview_awb(c3_dng_reg_reader_t reader);
/* A verified, already locked preview WB (COM8 AWB off). */
uint8_t c3_dng_capture_locked_awb(uint8_t red,uint8_t green,uint8_t blue);
/* Pure conversion of validated SCCB AWB values. */
uint8_t c3_dng_neutral_from_awb(uint8_t red, uint8_t green, uint8_t blue,
                                uint32_t out_per100k[3]);
/* True if current held frame uses sensor AWB instead of fixed fallback. */
uint8_t c3_dng_wb_sensor_valid(void);
/* The unmodified sensor AWB register values captured for THIS shot.
 * Only available after a validated dynamic WB snapshot; never invent gains
 * from the DNG tint-reversed AsShotNeutral values. */
uint8_t c3_dng_get_preview_awb_gains(uint8_t *red, uint8_t *green,
                                    uint8_t *blue);
/* Set once per HELD RAW frame, after final capture and before initial save.
 * manual_dial: 0..8, or 0xFF for P. gain_reg: actual/commanded OV7725
 * GAIN or 0xFF unknown. auto_rows: RAW AEC rows at v177 auto timing,
 * 0 when unknown. Retained across SD retries; begin_shot resets to 1 DN. */
void c3_dng_set_black_level(uint8_t manual_dial, uint8_t gain_reg,
                            uint16_t auto_rows);
/* Coherent, non-FF register reads after FIFO hold; use commanded values
 * on read failure. Only used to select metadata, never to alter exposure. */
void c3_dng_capture_black_level(uint8_t manual_dial, uint8_t commanded_gain,
                                uint16_t commanded_auto_rows,
                                c3_dng_reg_reader_t reader);
void c3_dng_make_header(uint8_t out[C3_DNG_HEADER_BYTES]);
/* After 642x480 Bayer bytes: model+profile name+FLOAT ToneCurve;
 * v179 adds held AsShotNeutral and BlackLevel rationals. The IFD points
 * here; TIFF StripByteCounts covers ONLY RAW. */
void c3_dng_make_tail(uint8_t out[C3_DNG_TAIL_BYTES]);
#endif

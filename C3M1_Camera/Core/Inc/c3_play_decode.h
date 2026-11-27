/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_PLAY_DECODE_H
#define C3_PLAY_DECODE_H
#include <stdint.h>
/* A deliberately bounded reader for C3M1's own uncompressed photo formats.
 * No dynamic allocation, no pointer arithmetic into unchecked TIFF offsets. */
#define C3_PLAY_W 320u
#define C3_PLAY_H 240u
#define C3_PLAY_ROW_MAX (642u * 2u * 2u)
typedef uint8_t (*c3_play_read_t)(void *ctx,uint32_t offset,uint8_t *dst,uint32_t n);
typedef struct { c3_play_read_t read; void *ctx; uint32_t size; } c3_play_file_t;
typedef struct {
    uint32_t offset, row_bytes;
    uint16_t width;
    uint8_t raw, sample_bytes, reverse_y, reverse_x;
    uint16_t lut[4][256]; /* BGGR, linear Q12 after black + WB */
    int32_t matrix[9]; /* camera RGB after WB -> display-linear RGB, Q12 */
} c3_play_image_t;
/* Key = photo number * 2 + (DNG ? 1 : 0); zero means invalid. */
uint32_t c3_play_name_key(const char *name);
void c3_play_path(uint32_t key,char path[16]);
/* 'dng' is the expected file type, not guessed from extension contents. */
uint8_t c3_play_parse(const c3_play_file_t *f,uint8_t dng,c3_play_image_t *im);
/* source_row is 0..239 in FILE order. Read row(s) sequentially, then
 * render in display order; display_y() accounts for TIFF Orientation. */
uint16_t c3_play_display_y(const c3_play_image_t *im,uint16_t source_row);
uint32_t c3_play_row_offset(const c3_play_image_t *im,uint16_t source_row);
void c3_play_render_row(const c3_play_image_t *im,const uint8_t *row,uint8_t out[640]);
#endif

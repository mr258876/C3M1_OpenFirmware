/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_FONT_H
#define C3_FONT_H
#include <stdint.h>
/* 5x7 ASCII A-Z, 0-9 and decimal point, column bits 0..6 = glyph rows 0..6.
 * Returns NULL for unsupported characters (including space).
 */
const uint8_t *c3_font_glyph(char c);
#endif

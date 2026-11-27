#ifndef C3_FONT_H
#define C3_FONT_H
#include <stdint.h>
/* 5x7 ASCII A-Z, 0-9 and decimal point, column bits 0..6 = glyph rows 0..6.
 * Returns NULL for unsupported characters (including space).
 */
const uint8_t *c3_font_glyph(char c);
#endif

#include "c3_font.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 assert(c3_font_glyph('%')!=0);
 assert(c3_font_glyph('~')!=0);
 assert(c3_font_glyph('V')!=0);
#if C3_OSD_SHUTTER_TIME
 assert(c3_font_glyph('/')!=0);
#endif
 puts("Battery UI glyphs V, %, ~ PASS");
 return 0;
}

#ifndef C3_OSD_HEADER_INCLUDED
#define C3_OSD_HEADER_INCLUDED
#include <stdint.h>
#ifndef C3_OSD_SHUTTER_TIME
#define C3_OSD_SHUTTER_TIME 1
#endif
#include "c3_meter.h"

/* 320x240 preview; only the top/bottom 24 rows need glyph masks.
 * One bit per pixel: white OSD foreground, 1920 bytes of SRAM total.
 * Camera data is never copied into this mask.
 */
#define C3_OSD_W 320u
#define C3_OSD_H 240u
#define C3_OSD_BAND_H 24u
#define C3_OSD_STRIDE (C3_OSD_W / 8u)

extern uint8_t c3_osd_mask[2][C3_OSD_BAND_H][C3_OSD_STRIDE];
/* Update the OSD icon from the existing battery ADC; no LCD writes here.
 * known=0 means not measured / ADC failure, percent is ignored. */
void c3_osd_set_battery(uint8_t percent, uint8_t known);
void c3_osd_prepare(uint32_t next_number, uint8_t sd_ok,
                    uint8_t grid, uint8_t monochrome,
                    uint8_t peaking, uint8_t focus_score_enabled,
                    uint8_t focus_score,
                    uint8_t fps_enabled, uint16_t fps_tenths,
                    uint8_t shoot_mode, uint8_t shutter_code,
                    uint8_t gain_step, uint8_t raw_dial_code,
                    int8_t ev_half_steps, uint8_t ev_active,
                    uint8_t zoom4x, uint8_t output_format, const c3_meter_t *meter);

/* Fault phase starts visible; use unsigned elapsed ticks across wrap. Call
 * once per preview frame before preparing the mask. Red is applied to the
 * existing second-cell glyphs only, not to the background or other text. */
#define C3_OSD_CARD_X0 80u
#define C3_OSD_CARD_X1 160u
#define C3_OSD_CARD_Y0 221u
#define C3_OSD_CARD_Y1 235u
uint8_t c3_osd_update_card_warning(uint8_t sd_ok, uint32_t now_ms);
/* Numeric-only blink; keep yellow AUTO badges. Called before prepare. */
void c3_osd_update_exposure_warning(const c3_meter_t *meter,
                                    uint8_t shoot_mode, uint32_t now_ms);
void c3_osd_color_card_row(uint8_t *line, const uint8_t *mask);
static inline uint16_t c3_osd_glyph_color(uint8_t card_red, uint16_t x, uint16_t y)
{
    return card_red && x >= C3_OSD_CARD_X0 && x < C3_OSD_CARD_X1 &&
        y >= C3_OSD_CARD_Y0 && y < C3_OSD_CARD_Y1 ? 0xF800u : 0xFFFFu;
}

/* AUTO badges occupy only the reserved last 13 pixels of the two cells.
 * Call on actual glyph pixels only; no extra mask or framebuffer needed. */
#define C3_OSD_AUTO_Y0 220u
#define C3_OSD_AUTO_Y1 235u
static inline uint16_t c3_osd_glyph_color_auto(uint8_t card_red,
        uint8_t shutter_auto, uint8_t gain_auto, uint16_t x, uint16_t y)
{
    if (y >= C3_OSD_AUTO_Y0 && y < C3_OSD_AUTO_Y1 &&
        ((shutter_auto && x >= 67u && x < 80u) ||
         (gain_auto && x >= 307u && x < 320u))) return 0xFFE0u;
    return c3_osd_glyph_color(card_red,x,y);
}
void c3_osd_color_auto_row(uint8_t *line, const uint8_t *mask,
                           uint8_t shutter_auto, uint8_t gain_auto);

/* Call only for y < 24 or y >= 216. No costly per-pixel lookup function. */
static inline const uint8_t *c3_osd_row(uint16_t y)
{
    if (y < C3_OSD_BAND_H) return c3_osd_mask[0][y];
    return c3_osd_mask[1][y - (C3_OSD_H - C3_OSD_BAND_H)];
}

#endif

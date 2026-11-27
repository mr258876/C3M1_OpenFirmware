/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_osd.h"
#include "c3_font.h"
#include "c3_exposure.h"
#include "c3_settings.h"
#include "c3_lcd_dma.h"
#include <string.h>

uint8_t c3_osd_mask[2][C3_OSD_BAND_H][C3_OSD_STRIDE];

/* Cache the completed mask; most video frames have identical overlay text. */
static uint32_t last_number;
static uint8_t last_sd, last_grid, last_mono, last_peak, last_score, last_score_enabled, last_fps_enabled, valid;
static uint16_t last_fps;
static uint8_t last_shutter, last_gain, last_mode, last_raw_dial, last_zoom4x, last_output_format;
static int8_t last_ev_half_steps;
static uint8_t last_ev_active;
static uint8_t card_fault, card_visible = 1u;
static uint32_t card_phase_start;
static uint8_t exposure_fault, exposure_hidden;
static uint32_t exposure_phase_start;

void c3_osd_update_exposure_warning(const c3_meter_t *meter,
                                    uint8_t shoot_mode, uint32_t now_ms)
{
    uint8_t fault=0u, hidden=0u;
    if (shoot_mode<2u && meter && meter->valid && meter->attempted &&
        now_ms-meter->last_attempt_ms < 3u*C3_METER_SAMPLE_MS)
        fault=meter->warning;
    if (fault) {
        if (fault!=exposure_fault) exposure_phase_start=now_ms;
        uint32_t elapsed=now_ms-exposure_phase_start;
        uint32_t phase=elapsed%1000u;
        exposure_phase_start+=elapsed-phase;
        hidden=(uint8_t)(phase>=500u ? (shoot_mode==0u ? 3u : 2u) : 0u);
    }
    exposure_fault=fault;
    if (hidden!=exposure_hidden) {
        exposure_hidden=hidden;
        valid=0u;
    }
}

uint8_t c3_osd_update_card_warning(uint8_t sd_ok, uint32_t now_ms)
{
    uint8_t visible = 1u;
    if (!sd_ok) {
        if (!card_fault) card_phase_start = now_ms;
        /* Advance by whole 1 s periods so tick wrap and delayed rendering
         * do not accumulate drift. Start every newly detected fault ON. */
        uint32_t elapsed = now_ms - card_phase_start;
        uint32_t phase = elapsed % 1000u;
        card_phase_start += elapsed - phase;
        visible = (uint8_t)(phase < 500u);
    }
    card_fault = (uint8_t)!sd_ok;
    if (visible != card_visible) {
        card_visible = visible;
        valid = 0u;
    }
    return (uint8_t)(card_fault && card_visible);
}

void c3_osd_color_card_row(uint8_t *line, const uint8_t *mask)
{
    /* RGB565 line is high-byte first; modify only set glyph bits in the
     * second 80px cell. Called after composition and before DMA submit. */
    for (unsigned m = C3_OSD_CARD_X0 / 8u; m < C3_OSD_CARD_X1 / 8u; ++m) {
        uint8_t bits = mask[m];
        if (!bits) continue;
        for (unsigned b = 0; b < 8u; ++b) {
            if (bits & (0x80u >> b)) {
                unsigned x = m * 8u + b;
                line[2u*x] = 0xF8u;
                line[2u*x+1u] = 0u;
            }
        }
    }
}
void c3_osd_color_auto_row(uint8_t *line, const uint8_t *mask,
                           uint8_t shutter_auto, uint8_t gain_auto)
{
    /* At most 24 pixels per badge scanline, only set glyph bits. */
    for (unsigned cell=0;cell<2u;++cell) {
        if (!(cell ? gain_auto : shutter_auto)) continue;
        unsigned x0=cell ? 307u : 67u;
        for (unsigned x=x0;x<x0+12u;++x) {
            if (mask[x>>3u] & (0x80u>>(x&7u))) {
                line[2u*x]=0xFFu;
                line[2u*x+1u]=0xE0u;
            }
        }
    }
}
/* v0.1.59 battery status belongs to the existing 1-bit top OSD band.
 * 255 means unknown (ADC failure or no sample); never pretend it is empty. */
static uint8_t osd_battery_percent = 255u;

void c3_osd_set_battery(uint8_t percent, uint8_t known)
{
    uint8_t value = known ? (percent > 100u ? 100u : percent) : 255u;
    if (value != osd_battery_percent) {
        osd_battery_percent = value;
        valid = 0u; /* Invalidate the cached mask only if the icon changes. */
    }
}

static uint8_t last_meter_valid;
static uint16_t last_meter_rows, last_meter_gain_x16;

static void set_pixel(int x, int y)
{
    if ((unsigned)x >= C3_OSD_W || (unsigned)y >= C3_OSD_H) return;
    unsigned band, row;
    if ((unsigned)y < C3_OSD_BAND_H) {
        band = 0u;
        row = (unsigned)y;
    } else if ((unsigned)y >= C3_OSD_H - C3_OSD_BAND_H) {
        band = 1u;
        row = (unsigned)y - (C3_OSD_H - C3_OSD_BAND_H);
    } else {
        return;
    }
    c3_osd_mask[band][row][(unsigned)x >> 3] |=
        (uint8_t)(0x80u >> ((unsigned)x & 7u));
}

/* Four white segments; x=214..233, y=7..18. The 24-row top
 * mask is shared by all overlays, so no extra LCD transactions are needed.
 * Compact FPS badge and value end before x=193; DNG starts at x=239. */
static void battery_icon(void)
{
    for (int x = 214; x <= 231; ++x) {
        set_pixel(x, 7);
        set_pixel(x, 18);
    }
    for (int y = 8; y <= 17; ++y) {
        set_pixel(214, y);
        set_pixel(231, y);
    }
    for (int y = 11; y <= 14; ++y)
        for (int x = 232; x <= 233; ++x)
            set_pixel(x, y);

    if (osd_battery_percent == 255u) {
        /* Unknown: slash instead of a misleading 0% bar. */
        for (int i = 0; i < 6; ++i)
            set_pixel(220 + i, 15 - i);
        return;
    }
    /* Rounding to nearest quarter, for a coarse indicative icon only. */
    unsigned bars = (unsigned)(osd_battery_percent + 12u) / 25u;
    if (bars > 4u) bars = 4u;
    for (unsigned b = 0u; b < bars; ++b)
        for (int y = 10; y <= 15; ++y)
            for (int x = 216 + (int)b * 4; x <= 218 + (int)b * 4; ++x)
                set_pixel(x, y);
}

static void text_step(int x, int y, const char *s, unsigned scale,
                      unsigned advance)
{
    for (; *s; ++s, x += (int)advance) {
        const uint8_t *f = c3_font_glyph(*s);
        if (!f) continue;
        for (unsigned col = 0; col < 5; ++col) {
            uint8_t bits = f[col];
            for (unsigned r = 0; r < 7; ++r) {
                if (!(bits & (1u << r))) continue;
                for (unsigned dy = 0; dy < scale; ++dy)
                    for (unsigned dx = 0; dx < scale; ++dx)
                        set_pixel(x + (int)(col * scale + dx),
                                  y + (int)(r * scale + dy));
            }
        }
    }
}

static void text(int x, int y, const char *s, unsigned scale)
{
    text_step(x, y, s, scale, 6u * scale);
}

/* Four fixed 80-pixel cells; 10x14 glyphs with a 1-pixel gap. Seven
 * characters occupy at most 76 pixels, even NO CARD or G31.0XA.
 * Center each value so it never pushes a neighboring field sideways.
 * The small-font fallback is only for unusually long diagnostic labels. */
static void bottom_value(unsigned cell, const char *s, uint8_t automatic)
{
    unsigned n = (unsigned)strlen(s);
    if (!n || cell >= 4u) return;
    unsigned available = automatic ? 67u : 80u;
    unsigned scale = n*11u-1u <= available ? 2u : 1u;
    unsigned advance = scale == 2u ? 11u : 6u;
    unsigned width = n * advance - 1u;
    if (width > available) return;
    if (!automatic || !((cell==0u && (exposure_hidden&1u)) ||
                        (cell==3u && (exposure_hidden&2u))))
        text_step((int)(cell * 80u + (available - width) / 2u),
                  216 + (int)((24u - 7u * scale) / 2u), s, scale, advance);
    if (automatic) {
        int x=(int)(cell*80u+67u);
        text(x,220,"AU",1);
        text(x,228,"TO",1);
    }
}
static void bottom_text(unsigned cell, const char *s)
{
    bottom_value(cell,s,0u);
}


static void decimal5(uint32_t n, char out[6])
{
    if (n > 99999u) n = 99999u;
    for (int i = 4; i >= 0; --i) {
        out[i] = (char)('0' + n % 10u);
        n /= 10u;
    }
    out[5] = '\0';
}

void c3_osd_prepare(uint32_t next_number, uint8_t sd_ok,
                    uint8_t grid, uint8_t monochrome,
                    uint8_t peaking, uint8_t focus_score_enabled,
                    uint8_t focus_score,
                    uint8_t fps_enabled, uint16_t fps_tenths,
                    uint8_t shoot_mode, uint8_t shutter_code,
                    uint8_t gain_step, uint8_t raw_dial_code,
                    int8_t ev_half_steps, uint8_t ev_active,
                    uint8_t zoom4x, uint8_t output_format, const c3_meter_t *meter)
{
    sd_ok = (uint8_t)!!sd_ok;
    grid = (uint8_t)!!grid;
    monochrome = (uint8_t)!!monochrome;
    peaking = (uint8_t)!!peaking;
    focus_score_enabled = (uint8_t)!!focus_score_enabled;
    fps_enabled = (uint8_t)!!fps_enabled;
    zoom4x = (uint8_t)!!zoom4x;
    if (shoot_mode > 2u) shoot_mode = 0u;
    if (shutter_code != C3_EXPOSURE_AUTO_CODE &&
        shutter_code >= C3_EXPOSURE_DEFINED_STEPS)
        shutter_code = C3_EXPOSURE_AUTO_CODE;
    raw_dial_code &= 0x0Fu;
    ev_active = (uint8_t)!!ev_active;
    if (ev_half_steps < -6) ev_half_steps = -6;
    if (ev_half_steps > 6) ev_half_steps = 6;
    if (gain_step > 5u) gain_step = 0u;
    if (focus_score > 99u) focus_score = 99u;
    if (fps_tenths > 999u) fps_tenths = 999u;
    uint8_t meter_valid = (uint8_t)(meter && meter->valid && shoot_mode != C3_SHOOT_M);
    uint16_t meter_rows = meter_valid ? meter->exposure_rows : 0u;
    uint16_t meter_gain_x16 = meter_valid ? meter->gain_x16 : 0u;
    if (valid && next_number == last_number && sd_ok == last_sd &&
        grid == last_grid && monochrome == last_mono &&
        peaking == last_peak && focus_score_enabled == last_score_enabled &&
        (!focus_score_enabled || focus_score == last_score) &&
        fps_enabled == last_fps_enabled &&
        (!fps_enabled || fps_tenths == last_fps) &&
        shutter_code == last_shutter && gain_step == last_gain &&
        shoot_mode == last_mode && raw_dial_code == last_raw_dial &&
        ev_half_steps == last_ev_half_steps && ev_active == last_ev_active &&
        zoom4x == last_zoom4x && output_format == last_output_format &&
        meter_valid == last_meter_valid &&
        meter_rows == last_meter_rows &&
        meter_gain_x16 == last_meter_gain_x16
        ) return;

    memset(c3_osd_mask, 0, sizeof(c3_osd_mask));
    char number[6];
    decimal5(next_number, number);
    static const char *const mode_names[3] = {"P", "S", "M"};
    text(8, 5, mode_names[shoot_mode], 2);
    /* Grid lines themselves indicate the grid setting; no GR label. */
    text(239, 5, output_format == C3_OUTPUT_BMP ? "BMP" : "DNG", 2);
    battery_icon();
    if (zoom4x) {
        /* 22x14 zoom value, with the 23x7 AE-L badge below it. */
        text(292,1,"4X",2);
        text(291,16,"AE-L",1);
    }
    if (focus_score_enabled) {
        char score[] = {(char)('0' + focus_score / 10u),
                        (char)('0' + focus_score % 10u), 0};
        /* Small two-row label, large numeric value (like AUTO badges). */
        text(56, 4, "F", 1);
        text(56, 12, "S", 1);
        text(65, 5, score, 2);
    }
    if (fps_enabled) {
        char fps[] = {(char)('0' + fps_tenths / 100u),
                      (char)('0' + (fps_tenths / 10u) % 10u),
                      '.', (char)('0' + fps_tenths % 10u), 0};
        text(130, 4, "FP", 1);
        text(133, 12, "S", 1);
        text(145, 5, fps, 2);
    }
    if (sd_ok) bottom_text(1u, number);
    else if (card_visible) bottom_text(1u, "NO CARD");
    {
        static const char *const gain_labels[6] = {
            "G---", "G1X", "G2X", "G4X", "G8X", "G16X"
        };
        /* OpenMV-style AEC-to-time, corrected for C3M1's extra EXHCL
         * horizontal dummy pixels (and 4X VGA vs 1X QVGA line length).
         * Auto uses sensor AEC readback; manual falls back to the theoretical
         * commanded AEC if a live SCCB sample is not available yet. */
        uint16_t shutter_rows = meter_valid ? meter_rows : 0u;
        if (!shutter_rows && shutter_code != C3_EXPOSURE_AUTO_CODE) {
            shutter_rows = c3_exposure_rows(shutter_code);
        }
        char exp[12];
        c3_meter_shutter_label(exp, shutter_rows,
                              c3_exposure_preview_row_ns_dial(zoom4x,shutter_code));
        bottom_value(0u, exp, shutter_code == C3_EXPOSURE_AUTO_CODE);
        /* Use the limited OSD width for the *requested* compensation.
         * Original dial debug code remains available in the MODE view. */
        if (ev_active) {
            char ev[7];
            c3_exposure_ev_label(ev, ev_half_steps);
            bottom_text(2u, ev);
        } else {
            bottom_text(2u, "EV N/A");
        }
        if (gain_step == 0u && meter_valid) {
            char gain[12];
            c3_meter_gain_label(gain, meter_gain_x16, 0u);
            bottom_value(3u, gain, 1u);
        } else {
            bottom_value(3u, gain_labels[gain_step], gain_step == 0u);
        }
    }
    last_number = next_number;
    last_sd = sd_ok;
    last_grid = grid;
    last_mono = monochrome;
    last_peak = peaking;
    last_score_enabled = focus_score_enabled;
    last_score = focus_score;
    last_fps_enabled = fps_enabled;
    last_fps = fps_tenths;
    last_shutter = shutter_code;
    last_gain = gain_step;
    last_mode = shoot_mode;
    last_raw_dial = raw_dial_code;
    last_ev_half_steps = ev_half_steps;
    last_ev_active = ev_active;
    last_zoom4x = zoom4x;
    last_output_format = output_format;
    last_meter_valid = meter_valid;
    last_meter_rows = meter_rows;
    last_meter_gain_x16 = meter_gain_x16;
    valid = 1;
}

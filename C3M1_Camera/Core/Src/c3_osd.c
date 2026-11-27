#include "c3_osd.h"
#include "c3_font.h"
#include "c3_exposure.h"
#include "c3_settings.h"
#include "c3_lcd_dma.h"
#include "c3_live_profile.h"
#if C3_PROFILE_HUD || C3_PEAK_PROFILE
#include "c3_camera.h"
#endif
#include <string.h>
#ifndef C3_OSD_SHUTTER_TIME
#define C3_OSD_SHUTTER_TIME 1
#endif

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

#if C3_PEAK_PROFILE
static void peak_profile_time(char *p,uint32_t cycles)
{
    uint32_t v=cycles/(SystemCoreClock/10000u);
    if(v>9999u)v=9999u;
    p[0]=(char)('0'+v/1000u);p[1]=(char)('0'+v/100u%10u);
    p[2]=(char)('0'+v/10u%10u);p[3]='.';p[4]=(char)('0'+v%10u);
}
#endif
#if C3_LIVE_PROFILE
static void live_dec(char *p,uint32_t n,unsigned digits)
{
    uint32_t lim=1u;for(unsigned i=0;i<digits;++i)lim*=10u;
    if(n>=lim)n=lim-1u;
    while(digits){p[--digits]=(char)('0'+n%10u);n/=10u;}
}
/* Four chars: 00.0 ms (saturated at 99.9). */
static void live_ms(char *p,uint32_t ticks)
{
    uint32_t d=ticks/7200u;if(d>999u)d=999u;
    live_dec(p,d/10u,2);p[2]='.';p[3]=(char)('0'+d%10u);
}
#endif
static uint8_t last_meter_valid;
static uint16_t last_meter_rows, last_meter_gain_x16;
#if C3_PROFILE_HUD
static uint32_t last_profile_vs, last_profile_wait, last_profile_draw10, last_profile_skips;
#if C3_STAGE_PROFILE
static uint32_t last_profile_row10, last_profile_dmaw10, last_profile_other10;
#if C3_ROW_DETAIL_PROFILE
static uint32_t last_profile_detail_a10, last_profile_detail_b10;
#endif
#endif
static void profile_dec3(char *p, uint32_t n)
{
    if (n > 999u) n = 999u;
    p[0] = (char)('0' + n / 100u);
    p[1] = (char)('0' + (n / 10u) % 10u);
    p[2] = (char)('0' + n % 10u);
}
#if C3_ROW_DETAIL_PROFILE
/* Five characters, tenths of a millisecond: 000.0 .. 999.9. */
static void profile_fixed_tenth(char *p, uint32_t tenths)
{
    if (tenths > 9999u) tenths = 9999u;
    profile_dec3(p, tenths / 10u);
    p[3] = '.';
    p[4] = (char)('0' + tenths % 10u);
}
#endif
static void profile_dec4(char *p, uint32_t n)
{
    if (n > 9999u) n = 9999u;
    p[0] = (char)('0' + n / 1000u);
    p[1] = (char)('0' + (n / 100u) % 10u);
    p[2] = (char)('0' + (n / 10u) % 10u);
    p[3] = (char)('0' + n % 10u);
}
#endif

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

#if !C3_PROFILE_HUD
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

#endif

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
#if C3_PROFILE_HUD
    /* One-frame-late telemetry; included in the EXISTING OSD mask, so the
     * LCD receives exactly the same full-frame transfer as the baseline.
     * No separate GRAM writes are added by the performance HUD. */
    uint32_t profile_vs = c3_preview_vsync_period_ms;
    uint32_t profile_wait = c3_preview_last_capture_ms;
    uint32_t profile_draw10 = c3_preview_last_draw_cycles / (SystemCoreClock / 10000u);
    uint32_t profile_skips = c3_camera_preview_frames_skipped();
#if C3_STAGE_PROFILE
    uint32_t profile_row10 = c3_preview_last_row_cycles / (SystemCoreClock / 10000u);
    uint32_t profile_dmaw10 = c3_lcd_dma_wait_cycles / (SystemCoreClock / 10000u);
    /* Unattributed overhead: FSMC window, DMA reprogramming, loop setup,
     * FIFO output gating, and unavoidable profiling instrumentation. */
    uint32_t profile_other10 = profile_draw10 > profile_row10 + profile_dmaw10 ?
                               profile_draw10 - profile_row10 - profile_dmaw10 : 0u;
#if C3_ROW_DETAIL_PROFILE
    uint32_t profile_detail_a10 = c3_preview_last_detail_a_cycles /
                                 (SystemCoreClock / 10000u);
    uint32_t profile_detail_b10 = c3_preview_last_detail_b_cycles /
                                 (SystemCoreClock / 10000u);
#if C3_ROW_DETAIL_PROFILE == 1
    /* 192 plain rows vs 48 OSD rows: scaled OSD extra is an estimate,
     * NOT a direct measurement of GPIO-only or OSD-only instructions. */
    uint32_t extra_cycles = c3_preview_last_detail_b_cycles >
                            c3_preview_last_detail_a_cycles / 4u ?
                            c3_preview_last_detail_b_cycles -
                            c3_preview_last_detail_a_cycles / 4u : 0u;
    uint32_t profile_extra10 = extra_cycles / (SystemCoreClock / 10000u);
#endif
#endif
#endif
#endif
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
#if C3_PROFILE_HUD
        profile_vs == last_profile_vs &&
        profile_wait == last_profile_wait &&
        profile_draw10 == last_profile_draw10 &&
        profile_skips == last_profile_skips &&
#if C3_STAGE_PROFILE
        profile_row10 == last_profile_row10 &&
        profile_dmaw10 == last_profile_dmaw10 &&
        profile_other10 == last_profile_other10 &&
#if C3_ROW_DETAIL_PROFILE
        profile_detail_a10 == last_profile_detail_a10 &&
        profile_detail_b10 == last_profile_detail_b10 &&
#endif
#endif
#endif
        meter_gain_x16 == last_meter_gain_x16
#if C3_PEAK_PROFILE || C3_LIVE_PROFILE
        && 0 /* previous frame timings update the diagnostic mask */
#endif
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
#if C3_ZOOM_AE_LOCK
        /* 22x14 zoom value, with the 23x7 AE-L badge below it. */
        text(292,1,"4X",2);
        text(291,16,"AE-L",1);
#else
        text(292,5,"4X",2);
#endif
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
#if C3_PROFILE_HUD
    /* 2x 7px glyph rows inside the existing 24px bottom OSD band.
     * VS: period between sensor VSYNC edges in milliseconds.
     * WAIT: acquire() blocking time, not full sensor exposure time.
     * DRAW: FIFO->CPU/OSD->LCD DMA until completion (DWT cycle count).
     * SKIP: cumulative FIFO producer frames dropped by the ring scheduler. */
#if C3_STAGE_PROFILE
#if C3_ROW_DETAIL_PROFILE == 1
    /* Actual RowOpt2 single-pass pixel loop: no extra raw buffer.
     * PLN+OSD approximately equals ROW; EXTRA subtracts the scaled plain
     * row baseline and is an estimate, not isolated OSD instruction time. */
    char profile_l1[] = "PLN 000.0 OSD 000.0";
    char profile_l2[] = "EXTRA 000.0 ROW 000.0";
    profile_fixed_tenth(&profile_l1[4], profile_detail_a10);
    profile_fixed_tenth(&profile_l1[14], profile_detail_b10);
    profile_fixed_tenth(&profile_l2[6], profile_extra10);
    profile_fixed_tenth(&profile_l2[16], profile_row10);
#elif C3_ROW_DETAIL_PROFILE == 2
    /* OSD rows use a scratch buffer to isolate GPIO fetch vs composition;
     * this intentionally differs from production single-pass RowOpt2. */
    char profile_l1[] = "READ 000.0 MIX 000.0";
    char profile_l2[] = "ROW 000.0 DRAW 000.0";
    profile_fixed_tenth(&profile_l1[5], profile_detail_a10);
    profile_fixed_tenth(&profile_l1[15], profile_detail_b10);
    profile_fixed_tenth(&profile_l2[4], profile_row10);
    profile_fixed_tenth(&profile_l2[15], profile_draw10);
#else
    /* Three disjoint CPU wall-time segments: ROW + DMAW + OTHER ~= DRAW.
     * ROW includes all CPU FIFO reads and OSD row composition; DMAW is only
     * the time the CPU explicitly blocks on an outstanding LCD DMA transfer.
     * DMA transfer cycles overlapped with ROW are not double-counted. */
    char profile_l1[] = "ROW 000.0 DW 000.0";
    char profile_l2[] = "OTHR 000.0 DRAW 000.0";
    profile_dec3(&profile_l1[4], profile_row10 / 10u);
    profile_l1[8] = (char)('0' + profile_row10 % 10u);
    profile_dec3(&profile_l1[13], profile_dmaw10 / 10u);
    profile_l1[17] = (char)('0' + profile_dmaw10 % 10u);
    profile_dec3(&profile_l2[5], profile_other10 / 10u);
    profile_l2[9] = (char)('0' + profile_other10 % 10u);
    profile_dec3(&profile_l2[16], profile_draw10 / 10u);
    profile_l2[20] = (char)('0' + profile_draw10 % 10u);
#endif /* C3_ROW_DETAIL_PROFILE */
#else
    char profile_l1[] = "VS 000ms WAIT 000ms";
    char profile_l2[] = "DRAW 000.0ms SKIP 0000";
    profile_dec3(&profile_l1[3], profile_vs);
    profile_dec3(&profile_l1[14], profile_wait);
    profile_dec3(&profile_l2[5], profile_draw10 / 10u);
    profile_l2[9] = (char)('0' + (profile_draw10 % 10u));
    profile_dec4(&profile_l2[18], profile_skips);
#endif
    text(8, 218, profile_l1, 1);
    text(8, 229, profile_l2, 1);
#if C3_OVERLAY_DIAG
    /* Read-only overlay A/B: status belongs in the existing 1-bit mask. */
    char flags[] = {'G', grid ? '1' : '0', ' ', 'P', peaking ? '1' : '0', 0};
    text(271, 229, flags, 1);
#endif
#elif C3_LIVE_PROFILE
    if(c3_live_profile.valid){
        const c3_live_profile_t *p=&c3_live_profile;
        char a[]="U00.0 P00.0 S00.0 C00.0 W00.0 L00.0 D00.0";
        live_ms(a+1,p->unpack);live_ms(a+7,p->peak);live_ms(a+13,p->score);
        live_ms(a+19,p->cpu);live_ms(a+25,p->wait);live_ms(a+31,p->lcd);live_ms(a+37,p->draw);
        char b[]="MAX0000 X0000 I0000 O00/00 E00/00 A0 FS0 R0 H0";
        live_dec(b+3,p->max_block/72u,4);live_dec(b+9,p->max_overlap/72u,4);
        live_dec(b+15,p->max_peak_overlap/72u,4);
        live_dec(b+21,p->over,2);live_dec(b+24,p->overlap,2);
        live_dec(b+28,p->peak_over,2);live_dec(b+31,p->peak_overlap,2);
        b[35]=(char)('0'+(p->peaking?p->area+1u:0u));
        b[39]=(char)('0'+p->mode);b[42]=(char)('0'+p->sample);b[45]=(char)('0'+p->shared);
        text(8,218,a,1);text(8,229,b,1);
    }else text(8,221,"DMA PROFILE WAIT / CPU",1);
#elif C3_PEAK_PROFILE
    /* One frame late, no extra LCD transfer. Times are milliseconds. */
    char pp1[]="F000.0 P000.0 S000.0 O000.0";
    char pp2[]="L000.0 D000.0 R0";
    peak_profile_time(pp1+1,c3_peak_profile.fetch);
    peak_profile_time(pp1+8,c3_peak_profile.peak);
    peak_profile_time(pp1+15,c3_peak_profile.score);
    peak_profile_time(pp1+22,c3_peak_profile.overlay);
    peak_profile_time(pp2+1,c3_peak_profile.lcd);
    peak_profile_time(pp2+8,c3_preview_last_draw_cycles);
    pp2[15]=(char)('0'+c3_peak_profile.phase);
    text(8,218,pp1,1);text(8,229,pp2,1);
#else
    if (sd_ok) bottom_text(1u, number);
    else if (card_visible) bottom_text(1u, "NO CARD");
    {
        static const char *const gain_labels[6] = {
            "G---", "G1X", "G2X", "G4X", "G8X", "G16X"
        };
#if C3_OSD_SHUTTER_TIME
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
#else
        if (shutter_code == C3_EXPOSURE_AUTO_CODE && meter_valid) {
            char exp[12];
            c3_meter_exposure_label(exp, meter_rows, 0u);
            bottom_value(0u, exp, shutter_code == C3_EXPOSURE_AUTO_CODE);
        } else if (shutter_code == C3_EXPOSURE_AUTO_CODE) {
            bottom_value(0u, "E---", 1u);
        } else {
            uint16_t r = c3_exposure_rows(shutter_code);
            char exp[] = {'E', (char)('0' + r / 100u),
                          (char)('0' + (r / 10u) % 10u),
                          (char)('0' + r % 10u), 'L', 0};
            bottom_value(0u, exp, shutter_code == C3_EXPOSURE_AUTO_CODE);
        }
#endif
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
#endif /* C3_PROFILE_HUD */
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
#if C3_PROFILE_HUD
    last_profile_vs = profile_vs;
    last_profile_wait = profile_wait;
    last_profile_draw10 = profile_draw10;
    last_profile_skips = profile_skips;
#if C3_STAGE_PROFILE
    last_profile_row10 = profile_row10;
    last_profile_dmaw10 = profile_dmaw10;
    last_profile_other10 = profile_other10;
#if C3_ROW_DETAIL_PROFILE
    last_profile_detail_a10 = profile_detail_a10;
    last_profile_detail_b10 = profile_detail_b10;
#endif
#endif
#endif
    valid = 1;
}

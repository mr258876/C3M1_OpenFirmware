/* Pure host-side test for 320x240 OSD rasterization and cache invalidation.
 * No MCU peripherals required; comparison against an independent reference
 * glyph rasterizer catches clipping, offset and stride regressions.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_osd.h"
#include "c3_font.h"
#include "c3_exposure.h"

static uint8_t expected[2][C3_OSD_BAND_H][C3_OSD_STRIDE];
static uint8_t expected_battery_level = 255u;

static void expected_dot(unsigned x, unsigned y)
{
    if (x >= 320u || y >= 24u) return;
    expected[0][y][x >> 3] |= (uint8_t)(0x80u >> (x & 7u));
}
static void expected_battery_icon(void)
{
    /* Independent reference bitmap: outline, terminal, slash / bar cells. */
    for (unsigned y = 7u; y <= 18u; ++y) {
        for (unsigned x = 214u; x <= 233u; ++x) {
            const uint8_t outline = (uint8_t)((x >= 214u && x <= 231u &&
                      (y == 7u || y == 18u || x == 214u || x == 231u)) ||
                      (x >= 232u && y >= 11u && y <= 14u));
            const uint8_t unknown = (uint8_t)(expected_battery_level == 255u &&
                    x >= 220u && x <= 225u && x + y == 235u);
            const unsigned bars = expected_battery_level == 255u ? 0u :
                    ((unsigned)expected_battery_level + 12u) / 25u;
            const uint8_t inside = (uint8_t)(expected_battery_level != 255u &&
                    y >= 10u && y <= 15u &&
                    x >= 216u && x <= 230u &&
                    ((x - 216u) / 4u) < bars && ((x - 216u) % 4u) < 3u);
            if (outline || unknown || inside) expected_dot(x,y);
        }
    }
}

static void expected_text_step(unsigned x0, unsigned y0, const char *str, unsigned scale, unsigned step)
{
    unsigned char_index = 0u;
    while (str[char_index]) {
        const uint8_t *glyph = c3_font_glyph(str[char_index]);
        if (glyph) {
            for (unsigned yy = 0; yy < 7u * scale; ++yy) {
                for (unsigned xx = 0; xx < 5u * scale; ++xx) {
                    if (!(glyph[xx / scale] & (1u << (yy / scale)))) continue;
                    unsigned x = x0 + char_index * step + xx;
                    unsigned y = y0 + yy;
                    if (x >= C3_OSD_W || y >= C3_OSD_H) continue;
                    unsigned band = y >= C3_OSD_H - C3_OSD_BAND_H;
                    if (!band && y >= C3_OSD_BAND_H) continue;
                    unsigned row = band ? y - (C3_OSD_H - C3_OSD_BAND_H) : y;
                    expected[band][row][x / 8u] |= (uint8_t)(0x80u >> (x & 7u));
                }
            }
        }
        ++char_index;
    }
}

static void expected_text(unsigned x0, unsigned y0, const char *str, unsigned scale)
{
    expected_text_step(x0,y0,str,scale,6u*scale);
}
static void expected_bottom_auto(unsigned cell, const char *str, unsigned automatic)
{
    unsigned n=(unsigned)strlen(str), available=automatic?67u:80u;
    unsigned scale=(n-1u)*11u+10u<=available?2u:1u, step=scale==2u?11u:6u;
    unsigned width=(n-1u)*step+5u*scale;
    expected_text_step(cell*80u+(available-width)/2u,
                       216u+(24u-7u*scale)/2u,str,scale,step);
    if(automatic) {
        expected_text(cell*80u+67u,220u,"AU",1u);
        expected_text(cell*80u+67u,228u,"TO",1u);
    }
}
static void expected_bottom(unsigned cell,const char *s) {expected_bottom_auto(cell,s,0);}

static int run_case(uint32_t count, unsigned sd_ok, unsigned grid, unsigned mono,
                    unsigned score_enabled, unsigned score, unsigned show_fps, unsigned fps_tenths,
                    unsigned mode, unsigned shutter, unsigned gain, unsigned raw,
                    int8_t ev_half, uint8_t ev_active,
                    unsigned zoom4x, const c3_meter_t *meter)
{
    (void)mono; (void)grid; /* no GR label; actual grid overlay is separate */
    char digits[6];
    memset(expected, 0, sizeof(expected));
    if (count > 99999u) count = 99999u;
    for (int j = 4; j >= 0; --j) {
        digits[j] = (char)('0' + count % 10u);
        count /= 10u;
    }
    digits[5] = '\0';
    static const char *const modes[3] = {"P", "S", "M"};
    expected_text(8u, 5u, modes[mode], 2u);
    expected_battery_icon();
    expected_text(239u, 5u, "DNG", 2u);
    if (zoom4x) {
#if C3_ZOOM_AE_LOCK
        expected_text(292u,1u,"4X",2u);
        expected_text(291u,16u,"AE-L",1u);
#else
        expected_text(292u,5u,"4X",2u);
#endif
    }
    if (score_enabled) {
        if (score > 99u) score = 99u;
        char label[] = {(char)('0' + score/10u),
                        (char)('0' + score%10u), 0};
        expected_text(56u,4u,"F",1u);
        expected_text(56u,12u,"S",1u);
        expected_text(65u,5u,label,2u);
    }
    if (show_fps) {
        if (fps_tenths > 999u) fps_tenths = 999u;
        char label[] = {(char)('0' + fps_tenths/100u),
                        (char)('0' + fps_tenths/10u%10u), '.',
                        (char)('0' + fps_tenths%10u), 0};
        expected_text(130u,4u,"FP",1u);
        expected_text(133u,12u,"S",1u);
        expected_text(145u,5u,label,2u);
    }
    expected_bottom(1u,sd_ok ? digits : "NO CARD");
    {
        static const char *const gain_names[6] = {"G---", "G1X", "G2X",
            "G4X", "G8X", "G16X"};
#if C3_OSD_SHUTTER_TIME
        uint16_t effective_rows = (meter && meter->valid && mode != 2u)
                                  ? meter->exposure_rows : 0u;
        if (!effective_rows && shutter != C3_EXPOSURE_AUTO_CODE)
            effective_rows=c3_exposure_rows((uint8_t)shutter);
        char exp[12];
        c3_meter_shutter_label(exp,effective_rows,c3_exposure_preview_row_ns_dial(zoom4x,(uint8_t)shutter));
        expected_bottom_auto(0u,exp,shutter==C3_EXPOSURE_AUTO_CODE);
#else
        if (shutter == C3_EXPOSURE_AUTO_CODE && meter && meter->valid && mode != 2u) {
            char e[15];
            snprintf(e, sizeof(e), "E%uL", meter->exposure_rows);
            expected_bottom_auto(0u,e,shutter==C3_EXPOSURE_AUTO_CODE);
        } else if (shutter == C3_EXPOSURE_AUTO_CODE) {
            expected_bottom_auto(0u,"E---",1);
        } else {
            unsigned r=c3_exposure_rows((uint8_t)shutter);
            char e[]={'E',(char)('0'+r/100u), (char)('0'+r/10u%10u),
                      (char)('0'+r%10u),'L',0};
            expected_bottom_auto(0u,e,shutter==C3_EXPOSURE_AUTO_CODE);
        }
#endif
        (void)raw;
        if (ev_active) {
            char ev[7];c3_exposure_ev_label(ev,ev_half);
            expected_bottom(2u,ev);
        } else expected_bottom(2u,"EV N/A");
        if (gain == 0u && meter && meter->valid && mode != 2u) {
            unsigned tenths = ((unsigned)meter->gain_x16 * 10u + 8u) / 16u;
            char g[15];
            snprintf(g, sizeof(g), "G%u.%uX", tenths / 10u, tenths % 10u);
            expected_bottom_auto(3u,g,1);
        } else expected_bottom_auto(3u,gain_names[gain],gain==0);
    }
    if (memcmp(expected, c3_osd_mask, sizeof(expected)) != 0) {
        for (unsigned band=0;band<2u;++band) for(unsigned y=0;y<24u;++y) for(unsigned x=0;x<320u;++x) {
            uint8_t m=(uint8_t)(0x80u>>(x&7u));
            uint8_t a=expected[band][y][x>>3]&m;
            uint8_t b=c3_osd_mask[band][y][x>>3]&m;
            if (!!a != !!b) {
                fprintf(stderr,"OSD pixel mismatch: band=%u x=%u y=%u act=%u exp=%u mode=%u shutter=%u zoom=%u\n",band,x,y,!!b,!!a,mode,shutter,zoom4x);
                return 1;
            }
        }
        return 1;
    }
    for (unsigned y = 0; y < C3_OSD_BAND_H; ++y) {
        if (memcmp(c3_osd_row((uint16_t)y), expected[0][y], C3_OSD_STRIDE) != 0) return 2;
        if (memcmp(c3_osd_row((uint16_t)(216u + y)), expected[1][y], C3_OSD_STRIDE) != 0) return 3;
    }
    /* Validate actual DMA post-color against independent expected yellow pixels,
     * with red card glyphs and arbitrary non-glyph background untouched. */
    for(unsigned y=220;y<235;y++) {
        uint8_t line[640],want[640];
        memset(line,0xA5,sizeof(line));
        const uint8_t *mask=c3_osd_row(y);
        for(unsigned x=0;x<320;x++) {
            if(mask[x/8]&(0x80u>>(x%8))) line[2*x]=line[2*x+1]=0xFF;
        }
        memcpy(want,line,sizeof(line));
        for(unsigned x=0;x<320;x++) if(mask[x/8]&(0x80u>>(x%8))) {
            unsigned yellow=(shutter==C3_EXPOSURE_AUTO_CODE && x>=67 && x<80) || (gain==0 && x>=307);
            unsigned red=!sd_ok && x>=80 && x<160 && y>=221 && y<235;
            uint16_t color=yellow?0xFFE0:red?0xF800:0xFFFF;
            want[2*x]=(uint8_t)(color>>8);want[2*x+1]=(uint8_t)color;
            if(c3_osd_glyph_color_auto(!sd_ok,shutter==C3_EXPOSURE_AUTO_CODE,gain==0,x,y)!=color) return 32;
        }
        if(!sd_ok && y>=221) c3_osd_color_card_row(line,mask);
        c3_osd_color_auto_row(line,mask,shutter==C3_EXPOSURE_AUTO_CODE,gain==0);
        if(memcmp(want,line,sizeof(line))) return 33;
    }
    return 0;
}

int main(void)
{
    /* Score-only display must invalidate the cache on value changes and
     * disappear when OFF, independently of the peaking flag. */
    uint8_t old_score[sizeof(c3_osd_mask)];
    c3_osd_prepare(1u,1u,0u,0u,0u,1u,12u,0u,0u,2u,
                   4u,3u,4u,0,0,0u,0u,NULL);
    memcpy(old_score,c3_osd_mask,sizeof(old_score));
    c3_osd_prepare(1u,1u,0u,0u,0u,1u,73u,0u,0u,2u,
                   4u,3u,4u,0,0,0u,0u,NULL);
    if (!memcmp(old_score,c3_osd_mask,sizeof(old_score))) return 40;
    c3_osd_prepare(1u,1u,0u,0u,0u,0u,73u,0u,0u,2u,
                   4u,3u,4u,0,0,0u,0u,NULL);
    for (unsigned y=0;y<24u;y++) for (unsigned x=56u;x<87u;x++)
        if (c3_osd_mask[0][y][x>>3]&(0x80u>>(x&7u))) return 41;
    /* The grid switch is no longer visible in the OSD mask. */
    uint8_t grid_mask[sizeof(c3_osd_mask)];
    memcpy(grid_mask,c3_osd_mask,sizeof(grid_mask));
    c3_osd_prepare(1u,1u,1u,0u,0u,0u,73u,0u,0u,2u,
                   4u,3u,4u,0,0,0u,0u,NULL);
    if(memcmp(grid_mask,c3_osd_mask,sizeof(grid_mask))) return 42;
    expected_battery_level = 53u;
    c3_osd_set_battery(53u,1u); /* start with two bars */
    struct args { uint32_t number; uint8_t sd, grid, mono, peak, score,
                  show_fps; uint16_t fps; uint8_t mode, shutter, gain, raw, zoom; } cases[] = {
        {1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, C3_EXPOSURE_AUTO_CODE, 0u, 0u, 0u},
        {2u, 0u, 1u, 0u, 1u, 72u, 0u, 0u, 1u, 0u, 1u, 0u, 1u},
        {99999u, 1u, 1u, 1u, 1u, 99u, 1u, 123u, 2u, 8u, 5u, 8u, 1u},
        {100000u, 0u, 0u, 1u, 0u, 0u, 1u, 999u, 0u, C3_EXPOSURE_AUTO_CODE, 4u, 15u, 0u},
        {1u, 1u, 0u, 0u, 1u, 17u, 1u, 0u, 2u, 7u, 2u, 15u, 1u},
        {1u, 1u, 0u, 0u, 1u, 18u, 1u, 54u, 1u, 6u, 3u, 6u, 0u},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        c3_osd_prepare(cases[i].number, cases[i].sd,
                       cases[i].grid, cases[i].mono, cases[i].peak, 1u,
                       cases[i].score, cases[i].show_fps, cases[i].fps,
                       cases[i].mode, cases[i].shutter, cases[i].gain,
                       cases[i].raw, 0, 1, cases[i].zoom, 0u, NULL);
        if (run_case(cases[i].number, cases[i].sd,
                     cases[i].grid, cases[i].mono, 1u,
                     cases[i].score, cases[i].show_fps, cases[i].fps,
                     cases[i].mode, cases[i].shutter, cases[i].gain,
                     cases[i].raw, 0, 1, cases[i].zoom, NULL) != 0) return 1;
        /* Reusing the same state should preserve the fully built mask. */
        uint8_t backup[sizeof(c3_osd_mask)];
        memcpy(backup, c3_osd_mask, sizeof(backup));
        c3_osd_prepare(cases[i].number, cases[i].sd,
                       cases[i].grid, cases[i].mono, cases[i].peak, 1u,
                       cases[i].score, cases[i].show_fps, cases[i].fps,
                       cases[i].mode, cases[i].shutter, cases[i].gain,
                       cases[i].raw, 0, 1, cases[i].zoom, 0u, NULL);
        if (memcmp(backup, c3_osd_mask, sizeof(backup)) != 0) return 2;
    }
    c3_osd_prepare(7u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u,
                   C3_EXPOSURE_AUTO_CODE, 0u, 0u, 0, 1, 0u, 0u, NULL);
    uint8_t old_mode_mask[sizeof(c3_osd_mask)];
    memcpy(old_mode_mask, c3_osd_mask, sizeof(old_mode_mask));
    c3_osd_prepare(7u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 2u,
                   C3_EXPOSURE_AUTO_CODE, 0u, 0u, 0, 1, 0u, 0u, NULL);
    if (memcmp(old_mode_mask, c3_osd_mask, sizeof(old_mode_mask)) == 0) return 3;
    c3_osd_prepare(7u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 2u,
                   C3_EXPOSURE_AUTO_CODE, 0u, 0u, 0, 1, 1u, 0u, NULL);
    uint8_t zoom_mask[sizeof(c3_osd_mask)];
    memcpy(zoom_mask, c3_osd_mask, sizeof(zoom_mask));
    c3_osd_prepare(7u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 2u,
                   C3_EXPOSURE_AUTO_CODE, 0u, 0u, 0, 1, 0u, 0u, NULL);
    if (memcmp(zoom_mask, c3_osd_mask, sizeof(zoom_mask)) == 0) return 4;
    /* Live AEC + AGC must invalidate the cached mask, with measured glyphs. */
    c3_meter_t live={.exposure_rows=173u,.gain_x16=40u,.valid=1u};
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u, 0u,&live);
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u,&live) != 0) return 5;
    uint8_t first_meter_mask[sizeof(c3_osd_mask)];
    memcpy(first_meter_mask,c3_osd_mask,sizeof(first_meter_mask));
    live.exposure_rows=250u; live.gain_x16=80u;
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u, 0u,&live);
    if (memcmp(first_meter_mask,c3_osd_mask,sizeof(first_meter_mask))==0) return 6;
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u,&live) != 0) return 7;
    /* S: the dial fixes exposure; AGC continues updating the gain display. */
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,1u,
                   7u,0u,7u, 0, 1,0u, 0u,&live);
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,1u,
                 7u,0u,7u, 0, 1,0u,&live) != 0) return 8;
    live.valid=0u;
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u, 0u,&live);
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u, 0, 1,0u,&live) != 0) return 9;
    /* EV half-stop values change OSD at the next frame; inactive auto control
     * must not claim compensation is modifying manual RAW exposure. */
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,3,1,0u, 0u,&live);
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u,3,1,0u,&live) != 0) return 10;
    uint8_t plus_ev_mask[sizeof(c3_osd_mask)];
    memcpy(plus_ev_mask,c3_osd_mask,sizeof(plus_ev_mask));
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,-3,1,0u, 0u,&live);
    if (memcmp(plus_ev_mask,c3_osd_mask,sizeof(plus_ev_mask))==0) return 11;
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u,-3,1,0u,&live) != 0) return 12;
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,2u,
                   7u,2u,7u,4,0,0u, 0u,&live);
    if (run_case(8u,1u,0u,0u,0u,0u,0u,0u,2u,
                 7u,2u,7u,4,0,0u,&live) != 0) return 13;
    uint8_t dng_label[sizeof(c3_osd_mask)];
    memcpy(dng_label,c3_osd_mask,sizeof(dng_label));
    c3_osd_prepare(8u,1u,0u,0u,0u,0u,0u,0u,0u,2u,
                   7u,2u,7u,4,0,0u,1u,&live);
    if (memcmp(dng_label,c3_osd_mask,sizeof(dng_label))==0) return 14;
    /* Test that a change in battery state invalidates a same-frame cache.
     * 0,25,50,75,100 and unknown include all fill/symbol branches. */
    const uint8_t test_levels[] = {0u,25u,50u,75u,100u,255u,49u,50u};
    for (unsigned i = 0u; i < sizeof(test_levels); ++i) {
        expected_battery_level = test_levels[i];
        c3_osd_set_battery(expected_battery_level,
                           (uint8_t)(expected_battery_level != 255u));
        c3_osd_prepare(8u,1u,0u,0u,1u,1u,28u,1u,999u,2u,
                       7u,2u,7u,4,0,1u,0u,&live);
        if (run_case(8u,1u,0u,0u,1u,28u,1u,999u,2u,
                     7u,2u,7u,4,0,1u,&live) != 0) return 20;
        /* No top-band bits should intrude into the gap to DNG. */
        for (unsigned x=234u; x<239u; ++x)
            for (unsigned y=7u; y<19u; ++y)
                if ((c3_osd_mask[0][y][x>>3] >> (7u-(x&7u))) & 1u)
                    return 21;
    }
    c3_osd_set_battery(250u,1u); /* Clamp >100 to four bars. */
    expected_battery_level=100u;
    c3_osd_prepare(8u,1u,0u,0u,1u,1u,28u,1u,999u,2u,
                   7u,2u,7u,4,0,1u,0u,&live);
    if (run_case(8u,1u,0u,0u,1u,28u,1u,999u,2u,
                 7u,2u,7u,4,0,1u,&live) != 0) return 22;
    /* SD state transition: only the second cell changes. */
    uint8_t sd_mask[sizeof(c3_osd_mask)];
    c3_osd_prepare(12345u,1u,1u,0u,1u,1u,99u,1u,999u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,0u,&live);
    memcpy(sd_mask,c3_osd_mask,sizeof(sd_mask));
    c3_osd_prepare(12345u,0u,1u,0u,1u,1u,99u,1u,999u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,0u,&live);
    if (memcmp(sd_mask,c3_osd_mask,sizeof(sd_mask))==0) return 23;
    if (memcmp(sd_mask,c3_osd_mask,sizeof(c3_osd_mask[0]))!=0) return 24;
    for(unsigned y=0;y<24u;y++) {
        const uint8_t *prev=sd_mask+sizeof(c3_osd_mask[0])+40u*y;
        if(memcmp(prev,c3_osd_mask[1][y],10u) ||
           memcmp(prev+20u,c3_osd_mask[1][y]+20u,20u)) return 25;
    }
    if (run_case(12345u,0u,1u,0u,1u,99u,1u,999u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,&live)) return 26;
    memcpy(sd_mask,c3_osd_mask,sizeof(sd_mask));
    c3_osd_prepare(54321u,0u,1u,0u,1u,1u,99u,1u,999u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,0u,&live);
    if (memcmp(sd_mask,c3_osd_mask,sizeof(sd_mask))!=0) return 27;
    c3_osd_prepare(54321u,1u,1u,0u,1u,1u,99u,1u,999u,0u,
                   C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,0u,&live);
    if (run_case(54321u,1u,1u,0u,1u,99u,1u,999u,0u,
                 C3_EXPOSURE_AUTO_CODE,0u,7u,6,1u,0u,&live)) return 28;
    unsigned checked=0u;
    static const uint16_t rows[]={0u,1u,2u,3u,8u,25u,500u,8191u,8192u,65535u};
    static const uint8_t shutters[]={0,1,2,3,4,5,6,7,8,C3_EXPOSURE_AUTO_CODE};
    for(unsigned z=0;z<2u;z++) for(unsigned sd=0;sd<2u;sd++)
      for(unsigned sh=0;sh<sizeof(shutters);sh++) for(unsigned reg=0;reg<256u;reg++) {
        unsigned mode=reg%3u;
        unsigned gain=mode==2u?1u+reg%5u:0u;
        int8_t ev=(int8_t)((int)(reg%13u)-6);
        uint8_t active=mode!=2u;
        live.valid=1u; live.exposure_rows=rows[(reg+sh)%10u];
        live.gain_x16=c3_meter_decode_gain_x16((uint8_t)reg);
        c3_osd_prepare(99999u,sd,1u,0u,1u,1u,99u,1u,999u,mode,
                       shutters[sh],gain,7u,ev,active,z,0u,&live);
        if (run_case(99999u,sd,1u,0u,1u,99u,1u,999u,mode,
                     shutters[sh],gain,7u,ev,active,z,&live)) return 29;
        for(unsigned cell=0u;cell<4u;cell++) for(unsigned y=0;y<24u;y++) {
            unsigned x=cell*80u;
            const uint8_t *r=c3_osd_mask[1][y];
            unsigned auto_cell=(cell==0 && shutters[sh]==C3_EXPOSURE_AUTO_CODE)||(cell==3 && gain==0);
            if((r[x/8u]&(auto_cell?0x80u:0xC0u)) || (r[x/8u+9u]&0x03u)) return 30;
            if((y<(auto_cell?4u:5u) || y>=19u) &&
               memcmp(r+x/8u,(uint8_t[10]){0},10u)!=0) return 31;
        }
        ++checked;
      }
    printf("PASS %u four-cell raster combinations, SD state transitions, AUTO badge colors and cell gutters\n",checked);
    printf("OSD raster verified: %zu bytes, including battery 0-100/unknown, mode left, FPS shift and all other overlays\n",
           sizeof(c3_osd_mask));
    return 0;
}

#include "c3_exposure.h"
#include "c3_rgb_timing.h"
#include "c3_shutter_timing.h"


/* Physical scale read from the photographed knob and the factory 00..08
 * display behaviour. NOT calibrated sensor exposure time. A nominal
 * ~62.5 us/line gives the approximate integer-row steps below, but the
 * actual preview and RAW line periods must be measured. In particular
 * code 00 (1/8000) may need fine AEC via LAEC (0x1F). */
static const uint16_t shutter_denominators[C3_EXPOSURE_DEFINED_STEPS] = {
    8000u, 4000u, 2000u, 1000u, 500u, 300u, 150u, 100u, 50u
};
/* OV7725 Rev 1.2 frame-timing figs. 6/7: VGA 784*tP, QVGA 576*tP;
 * tP=tPCLK for RAW, tP=2*tPCLK for RGB565. Current RGB initialization
 * adds configurable 0x9E/0x80/0x40/0 horizontal dummy pixels via EXHCL;
 * RAW has EXHCL=0. Changes to EXHCL MUST change shutter row timing too.
 * 4x zoom selects VGA centre crop but keeps RGB565 8-bit bus output.
 * CLKRC=1 doubles the internal period relative to CLKRC=0.
 * C3_SENSOR_PCLK_HZ=12 MHz is corroborated by the observed 29.4 fps
 * (278 QVGA lines/frame, effective 1468 PCLK intervals per line).
 * Formula is a datasheet-based model, NOT laboratory exposure calibration. */
#define C3_PREVIEW_QVGA_PCLK_TICKS ((576u + C3_RGB_EXHCL) * 2u)
#define C3_PREVIEW_ZOOM_PCLK_TICKS ((784u + C3_RGB_EXHCL) * 2u)
#define C3_RAW_VGA_PCLK_TICKS 784u
static uint8_t preview_is_zoom4x;
static c3_timing_format_t timing_format(c3_exposure_sensor_mode_t mode)
{
    if (mode==C3_EXPOSURE_RAW) return C3_TIMING_RAW8;
    if (mode==C3_EXPOSURE_BMP) return C3_TIMING_RGB1X;
    return preview_is_zoom4x?C3_TIMING_RGB4X:C3_TIMING_RGB1X;
}
void c3_exposure_set_preview_zoom(uint8_t zoom4x)
{
    preview_is_zoom4x=(uint8_t)(zoom4x != 0u);
}
static uint32_t exposure_line_ticks(c3_exposure_sensor_mode_t mode,
                                    uint8_t raw_clkrc)
{
    if (mode == C3_EXPOSURE_RAW) {
        if (raw_clkrc > 1u) return 0u;
        return C3_RAW_VGA_PCLK_TICKS * ((uint32_t)raw_clkrc + 1u);
    }
    if (mode == C3_EXPOSURE_PREVIEW)
        return preview_is_zoom4x ? C3_PREVIEW_ZOOM_PCLK_TICKS
                                 : C3_PREVIEW_QVGA_PCLK_TICKS;
    if (mode == C3_EXPOSURE_BMP)
        return C3_PREVIEW_QVGA_PCLK_TICKS;
    return 0u;
}
uint32_t c3_exposure_preview_row_ns_dial(uint8_t zoom4x, uint8_t dial)
{
    c3_shutter_plan_t p;
    const c3_timing_format_t fmt=zoom4x?C3_TIMING_RGB4X:C3_TIMING_RGB1X;
    if (c3_shutter_plan(fmt,dial,(uint8_t)(dial!=C3_EXPOSURE_AUTO_CODE),&p))
        return c3_shutter_row_ns(fmt,&p);
    return c3_exposure_preview_row_ns(zoom4x);
}
uint32_t c3_exposure_preview_row_ns(uint8_t zoom4x)
{
    const uint32_t ticks = zoom4x ? C3_PREVIEW_ZOOM_PCLK_TICKS
                                   : C3_PREVIEW_QVGA_PCLK_TICKS;
    return (uint32_t)(((uint64_t)ticks * 1000000000ull +
                      C3_SENSOR_PCLK_HZ/2u) / C3_SENSOR_PCLK_HZ);
}
uint32_t c3_exposure_estimated_row_ns(c3_exposure_sensor_mode_t mode,
                                     uint8_t raw_clkrc)
{
    uint32_t ticks=exposure_line_ticks(mode,raw_clkrc);
    return ticks ? (uint32_t)(((uint64_t)ticks * 1000000000ull +
                         C3_SENSOR_PCLK_HZ/2u)/C3_SENSOR_PCLK_HZ) : 0u;
}
uint16_t c3_exposure_rows_for_mode(uint8_t code,
                                   c3_exposure_sensor_mode_t mode,
                                   uint8_t raw_clkrc)
{
    if (!c3_exposure_dial_defined(code)) return 0u;
    if (raw_clkrc==0u) {
        c3_shutter_plan_t plan;
        if (c3_shutter_plan(timing_format(mode),code,1u,&plan)) return plan.rows;
    }
    uint32_t ticks=exposure_line_ticks(mode,raw_clkrc);
    if (!ticks) return 0u;
    uint32_t denom=(uint32_t)shutter_denominators[code] * ticks;
    /* Round to closest full exposure row; LAEC fine sub-row encoding is
     * not specified precisely enough in this datasheet for safe use. */
    uint32_t rows=(C3_SENSOR_PCLK_HZ + denom/2u)/denom;
    if (!rows) rows=1u;
    if (rows>8191u) rows=8191u;
    return (uint16_t)rows;
}
/* OV7725 gain code, not a calibrated ISO number. */
static const uint8_t gains[C3_EXPOSURE_GAIN_STEPS] = {
    0x00u, 0x00u, 0x10u, 0x30u, 0x70u, 0xF0u
};
/* AEC/AGC target operating regions (AEB=lower, AEW=upper, VPT=fast
 * control region). All values are constrained to 8-bit luminance. The
 * one-half-EV menu labels are NOMINAL; these entries are a first-pass
 * nonlinear luminance-target sweep, not a physically calibrated f-stop
 * transformation. The real image-space EV response depends on gamma,
 * saturation, sensor timing and AEC/AGC convergence. Bench-calibrate it.
 * 0 EV preserves the exact previous stable region (40/30/A1).
 */
typedef struct { uint8_t aew, aeb, vpt; } c3_ae_region_t;
static const c3_ae_region_t ev_regions[13] = {
    { 17u, 13u, 0x30u }, /* -3.0 */
    { 21u, 16u, 0x30u }, /* -2.5 */
    { 26u, 20u, 0x40u }, /* -2.0 */
    { 33u, 25u, 0x50u }, /* -1.5 */
    { 41u, 31u, 0x60u }, /* -1.0 */
    { 51u, 38u, 0x81u }, /* -0.5 */
    { 64u, 48u, 0xA1u }, /*  0.0 */
    { 80u, 60u, 0xB1u }, /* +0.5 */
    {100u, 75u, 0xC1u }, /* +1.0 */
    {125u, 94u, 0xE2u }, /* +1.5 */
    {156u,117u, 0xF2u }, /* +2.0 */
    {195u,146u, 0xF4u }, /* +2.5 */
    {228u,183u, 0xF7u }  /* +3.0, clipped to fast-zone range */
};
static int8_t ev_clamp(int n)
{
    if (n > 6) n=6;
    if (n < -6) n=-6;
    return (int8_t)n;
}
void c3_exposure_ev_targets(int8_t steps, uint8_t *aew, uint8_t *aeb, uint8_t *vpt)
{
    const c3_ae_region_t *r = &ev_regions[(unsigned)(ev_clamp(steps)+6)];
    if (aew) *aew = r->aew;
    if (aeb) *aeb = r->aeb;
    if (vpt) *vpt = r->vpt;
}
void c3_exposure_ev_label(char out[7], int8_t steps)
{
    unsigned abs = (unsigned)(steps < 0 ? -((int)steps) : steps);
    out[0]='E'; out[1]='V'; out[2]=steps<0?'-':'+';
    out[3]=(char)('0'+abs/2u); out[4]='.';
    out[5]=(char)('0'+5u*(abs&1u)); out[6]='\0';
}
uint8_t c3_exposure_ev_adjust(c3_exposure_t *s, int steps)
{
    if (!s) return 0u;
    int8_t next=ev_clamp((int)s->ev_half_steps+steps);
    if (s->ev_half_steps==next) return 0u;
    s->ev_half_steps=next;
    return 1u;
}
uint8_t c3_exposure_ev_active(const c3_exposure_t *s)
{
    if (!s) return 0u;
    return (uint8_t)(s->shoot_mode == C3_SHOOT_P ||
                     s->shoot_mode == C3_SHOOT_S);
}


uint8_t c3_exposure_decode_gpio(uint16_t idr)
{
    return (uint8_t)((idr >> 6u) & 0x0Fu);
}
uint8_t c3_exposure_dial_defined(uint8_t code)
{
    return (uint8_t)(code < C3_EXPOSURE_DEFINED_STEPS);
}
uint16_t c3_exposure_target_denominator(uint8_t code)
{
    return c3_exposure_dial_defined(code) ? shutter_denominators[code] : 0u;
}
uint16_t c3_exposure_rows(uint8_t code)
{
    return c3_exposure_rows_for_mode(code,C3_EXPOSURE_PREVIEW,0u);
}

/* Single source of truth: there is no aperture-priority mode. Entering
 * AUTO shutter clears any manual gain, so the next labelled detent is S. */
static void update_shoot_mode(c3_exposure_t *s)
{
    if (!c3_exposure_dial_defined(s->dial_code)) {
        s->gain_step=0u;
        s->shoot_mode=C3_SHOOT_P;
    } else {
        s->shoot_mode=s->gain_step ? C3_SHOOT_M : C3_SHOOT_S;
    }
}

void c3_exposure_init(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms)
{
    s->dial_code = (uint8_t)(raw_dial & 0x0Fu);
    s->gain_step = 0u;
    s->ev_half_steps = 0;
    update_shoot_mode(s);
    s->candidate = s->dial_code;
    s->candidate_since = now_ms;
}

uint8_t c3_exposure_poll_dial(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms)
{
    uint8_t code = (uint8_t)(raw_dial & 0x0Fu);
    if (code != s->candidate) {
        s->candidate = code;
        s->candidate_since = now_ms;
    }
    if (code != s->dial_code &&
        (uint32_t)(now_ms - s->candidate_since) >= C3_EXPOSURE_DIAL_SETTLE_MS) {
        s->dial_code = code;
        update_shoot_mode(s);
        return 1u;
    }
    return 0u;
}

void c3_exposure_sync_dial(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms)
{
    if (!s) return;
    s->dial_code = (uint8_t)(raw_dial & 15u);
    s->candidate = s->dial_code;
    s->candidate_since = now_ms;
    update_shoot_mode(s);
}

void c3_exposure_gain_next(c3_exposure_t *s)
{
    if (!s || s->shoot_mode == C3_SHOOT_P) return;
    s->gain_step=(uint8_t)((s->gain_step+1u)%C3_EXPOSURE_GAIN_STEPS);
    update_shoot_mode(s);
}

uint8_t c3_exposure_effective_dial(const c3_exposure_t *s)
{
    return s && c3_exposure_dial_defined(s->dial_code) ?
           s->dial_code : C3_EXPOSURE_AUTO_CODE;
}

uint8_t c3_exposure_effective_gain(const c3_exposure_t *s)
{
    return s && c3_exposure_dial_defined(s->dial_code) ? s->gain_step : 0u;
}

uint8_t c3_exposure_gain_reg(uint8_t step)
{
    return gains[(step < C3_EXPOSURE_GAIN_STEPS) ? step : 0u];
}

/* v188: disabling COM5 alone may leave extended vertical frame timing
 * behind after P/night mode. Stop AEC/AGC before changing frame timing.
 * Clear both documented writable vertical dummy-row pairs; never touch
 * EXHCH/EXHCL here (those are the calibrated horizontal shutter plans).
 * P AUTO is intentionally excluded, including P still-capture timing:
 * long locked P exposures may rely on a longer frame period. */
static uint8_t clear_manual_night_rows(uint8_t base, c3_exposure_read_t rd,
                                      c3_exposure_write_t wr)
{
    static const uint8_t regs[] = {0x2Eu, 0x2Du, 0x34u, 0x33u};
    for (unsigned attempt=0u; attempt<2u; ++attempt) {
        uint8_t com8=0xFFu,com5=0xFFu;
        if (wr(0x13u,base) || !rd(0x13u,&com8) || (com8&0x05u)) continue;
        if (wr(0x0Eu,0u) || !rd(0x0Eu,&com5) || com5!=0u) continue;
        unsigned i;
        for (i=0u; i<sizeof(regs); ++i)
            if (wr(regs[i],0u)) break;
        if (i!=sizeof(regs)) continue;
        for (i=0u; i<sizeof(regs); ++i) {
            uint8_t v=0xFFu;
            if (!rd(regs[i],&v) || v!=0u) break;
        }
        if (i==sizeof(regs)) return 1u;
    }
    return 0u;
}

uint8_t c3_exposure_prepare_timing(const c3_exposure_t *s,
                                    c3_exposure_sensor_mode_t mode,
                                    c3_exposure_read_t rd,
                                    c3_exposure_write_t wr)
{
    if (!s || !rd || !wr || mode>C3_EXPOSURE_BMP) return 0u;
    const uint8_t dial=c3_exposure_effective_dial(s);
    const uint8_t manual=(uint8_t)(dial!=C3_EXPOSURE_AUTO_CODE);
    c3_shutter_plan_t p;
    if (!c3_shutter_plan(timing_format(mode),dial,manual,&p)) return 0u;
    /* EXHCH high nibble stores dummy[11:8], low nibble carries output size.
     * Never blindly overwrite or trust a floating 0xFF read. 320x240 RGB
     * and 644x480 RAW both require zero low bits on the current module. */
    uint8_t original=0xFFu;
    if ((!rd(0x2Au,&original) || original==0xFFu || (original&0x0Fu)!=0u) &&
        (!rd(0x2Au,&original) || original==0xFFu || (original&0x0Fu)!=0u))
        return 0u;
    const uint8_t new_high=(uint8_t)((p.dummy>>8u)<<4u);
    const uint8_t new_exh=(uint8_t)((original&0x0Fu)|new_high);
    const uint8_t base=(mode==C3_EXPOSURE_RAW)?0x00u:0xFAu;
    if (manual ? !clear_manual_night_rows(base,rd,wr) : wr(0x13u,base)) return 0u;
    if (wr(0x1Fu,0u) || wr(0x2Au,new_exh) ||
        wr(0x2Bu,(uint8_t)p.dummy) || wr(0x11u,p.clkrc)) return 0u;
    /* Real SCCB has no ACK; verify the actual values, including CLKRC[6].
     * A failure aborts capture and invokes existing preview recovery. */
    for (unsigned attempt=0u;attempt<2u;++attempt) {
        uint8_t hi=0xFFu,lo=0xFFu,clk=0xFFu;
        if (rd(0x2Au,&hi) && rd(0x2Bu,&lo) && rd(0x11u,&clk) &&
            hi==new_exh && lo==(uint8_t)p.dummy && clk==p.clkrc) return 1u;
    }
    return 0u;
}

uint8_t c3_exposure_apply(const c3_exposure_t *s,
                          c3_exposure_sensor_mode_t mode,
                          c3_exposure_write_t wr)
{
    if (!s || !wr || s->shoot_mode > C3_SHOOT_M ||
        s->gain_step >= C3_EXPOSURE_GAIN_STEPS ||
        s->dial_code >= C3_EXPOSURE_DIAL_STEPS ||
        s->shoot_mode != (!c3_exposure_dial_defined(s->dial_code) ? C3_SHOOT_P :
                         (s->gain_step ? C3_SHOOT_M : C3_SHOOT_S)) ||
        (s->shoot_mode == C3_SHOOT_P && s->gain_step != 0u) ||
        s->ev_half_steps < -6 || s->ev_half_steps > 6 ||
        mode > C3_EXPOSURE_BMP) return 0u;

    uint8_t dial = c3_exposure_effective_dial(s);
    uint8_t gain = c3_exposure_effective_gain(s); /* P/S: AGC */
    uint8_t auto_exposure = (uint8_t)(dial == C3_EXPOSURE_AUTO_CODE);
    /* Preview COM8 base = 0xFA; RAW base = 0x02. AEC bit0, AGC bit2.
     * Disable automatic controls while applying manual exposure. */
    /* RAW Bayer pixels do not use ISP AWB. The DNG neutral was captured
     * from the live RGB preview BEFORE changing COM7; do not run a second
     * RAW AWB convergence loop. Preserve AEC/AGC independently. */
    uint8_t base = (mode == C3_EXPOSURE_RAW) ? 0x00u : 0xFAu;
    uint8_t final = (uint8_t)(base | (auto_exposure ? 0x01u : 0u) |
                                     (gain == 0u ? 0x04u : 0u));

    if (wr(0x13u, base)) return 0u;
    if (wr(0x14u, (mode == C3_EXPOSURE_RAW) ? 0x30u : 0x31u)) return 0u;
    if (wr(0x0Eu, (mode == C3_EXPOSURE_PREVIEW && auto_exposure)
                    ? 0xF5u : 0x00u)) return 0u;
    /* AEC and AGC share the sensor target-luminance region. When neither
     * is automatic, restore 0 EV registers but do not alter manual gain
     * or exposure. Apply the same region to preview and RAW photo mode. */
    uint8_t aew, aeb, vpt;
    c3_exposure_ev_targets(c3_exposure_ev_active(s) ? s->ev_half_steps : 0,
                           &aew, &aeb, &vpt);
    if (wr(0x24u, aew)) return 0u;
    if (wr(0x25u, aeb)) return 0u;
    if (wr(0x26u, vpt)) return 0u;
    if (!auto_exposure) {
        uint16_t rows = c3_exposure_rows_for_mode(dial,mode,C3_RAW_CLKRC);
        /* LAEC sub-row value must not remain stale from auto exposure.
         * Explicitly zero it until a calibrated fractional-row path exists. */
        if (wr(0x1Fu, 0x00u)) return 0u;
        if (wr(0x08u, (uint8_t)(rows >> 8))) return 0u;
        if (wr(0x10u, (uint8_t)rows)) return 0u;
    }
    if (gain != 0u && wr(0x00u, c3_exposure_gain_reg(gain))) return 0u;
    if (wr(0x13u, final)) return 0u;
    return 1u;
}

/* v0.1.62: optional RAW still exposure inheritance.
 * Do not trust a single 0xFFFF read: this C3M1 historically had floating-SDA
 * SCCB failures.  An invalid sample never produces locked black/white shots;
 * main.c falls back to the original RAW initialization/settling sequence.
 */
uint8_t c3_exposure_snapshot_read(c3_exposure_snapshot_t *out,
                                  c3_exposure_read_t read_reg)
{
    if (!out) return 0u;
    out->valid = 0u;
    out->preview_rows = 0u;
    out->gain_register = 0u;
    if (!read_reg) return 0u;
    for (uint8_t retry=0u; retry<2u; ++retry) {
        uint8_t hi0, low, hi1, gain;
        if (!read_reg(0x08u,&hi0) || !read_reg(0x10u,&low) ||
            !read_reg(0x08u,&hi1)) return 0u;
        if (hi0 != hi1) continue; /* AEC update across byte boundary */
        if (!read_reg(0x00u,&gain)) return 0u;
        uint16_t rows = (uint16_t)(((uint16_t)hi1<<8u)|low);
        /* Restrict to plausible exposures. 0xFFFF is the classic C3M1
         * broken-SCCB signature; gain=FF is similarly suspect. */
        if (rows==0u || rows>8191u || gain==0xFFu) return 0u;
        out->preview_rows=rows;
        out->gain_register=gain;
        out->valid=1u;
        return 1u;
    }
    return 0u;
}

uint16_t c3_exposure_raw_rows(uint16_t preview_rows, uint8_t raw_clkrc)
{
    /* Preserve exposure time across QVGA/VGA format, pixel packing and CLKRC. */
    uint32_t src_ticks=exposure_line_ticks(C3_EXPOSURE_PREVIEW,0u);
    uint32_t dst_ticks=exposure_line_ticks(C3_EXPOSURE_RAW,raw_clkrc);
    if (!dst_ticks) return 0u;
    uint32_t rows=((uint32_t)preview_rows*src_ticks+dst_ticks/2u)/dst_ticks;
    if (rows==0u) rows=1u;
    if (rows>8191u) rows=8191u;
    return (uint16_t)rows;
}

uint8_t c3_exposure_apply_raw_locked(const c3_exposure_snapshot_t *shot,
                                     uint8_t raw_clkrc,
                                     c3_exposure_write_t wr)
{
    if (!shot || !shot->valid || !wr || raw_clkrc>1u ||
        shot->preview_rows==0u || shot->preview_rows>8191u ||
        shot->gain_register==0xFFu) return 0u;
    const uint16_t rows=c3_exposure_raw_rows(shot->preview_rows,raw_clkrc);
    /* Disable AEC/AGC while restoring the preview exposure snapshot.
     * When freezing RAW AWB, also clear COM8[1]. The separate preview AWB
     * snapshot is already stored in c3_dng.c for DNG metadata and RGB restore. */
    if (wr(0x13u,0x00u)) return 0u;
    if (wr(0x1Fu,0x00u)) return 0u; /* provisional fine AEC not calibrated */
    if (wr(0x08u,(uint8_t)(rows>>8u))) return 0u;
    if (wr(0x10u,(uint8_t)rows)) return 0u;
    if (wr(0x00u,shot->gain_register)) return 0u;
    return 1u;
}

uint8_t c3_exposure_apply_raw_locked_timed(const c3_exposure_snapshot_t *shot,
                                           const c3_exposure_t *state,
                                           uint8_t raw_clkrc,
                                           c3_exposure_write_t wr)
{
    if (!shot || !shot->valid || !state || !wr || raw_clkrc>1u ||
        shot->preview_rows==0u || shot->preview_rows>8191u ||
        shot->gain_register==0xFFu) return 0u;
    const uint8_t dial=c3_exposure_effective_dial(state);
    const uint16_t rows=(dial==C3_EXPOSURE_AUTO_CODE)?
        c3_exposure_raw_rows(shot->preview_rows,raw_clkrc):
        c3_exposure_rows_for_mode(dial,C3_EXPOSURE_RAW,raw_clkrc);
    if (!rows) return 0u;
    /* The prior generic RAW apply may turn AGC on in S. Lock the snapshot
     * gain at shot time, but keep the RAW-specific manual AEC rows. */
    if (wr(0x13u,0x00u) || wr(0x1Fu,0x00u) ||
        wr(0x08u,(uint8_t)(rows>>8u)) || wr(0x10u,(uint8_t)rows) ||
        wr(0x00u,shot->gain_register)) return 0u;
    return 1u;
}

/* This operation runs with COM8 AEC/AGC/AWB all disabled following the
 * no-reset RAW->RGB register switch. It is safe if the snapshot is invalid:
 * simply re-enable auto controls normally and let them converge naturally. */
uint8_t c3_exposure_restore_preview_snapshot(const c3_exposure_snapshot_t *shot,
                                             const c3_exposure_t *exposure,
                                             c3_exposure_write_t wr)
{
    if (!exposure || !wr) return 0u;
    if (!shot || !shot->valid) return 1u;
    if (c3_exposure_effective_dial(exposure) == C3_EXPOSURE_AUTO_CODE) {
        if (wr(0x08u,(uint8_t)(shot->preview_rows >> 8u)) ||
            wr(0x10u,(uint8_t)shot->preview_rows)) return 0u;
    }
    if (c3_exposure_effective_gain(exposure) == 0u &&
        wr(0x00u,shot->gain_register)) return 0u;
    return 1u;
}

static uint8_t zoom_expect(c3_exposure_read_t rd,uint8_t addr,uint8_t value,uint8_t mask)
{
    uint8_t v;
    for(unsigned i=0;i<2u;++i)
        if(rd(addr,&v) && (v!=0xFFu || (mask==0xFFu && value==0xFFu)) &&
           (v&mask)==(value&mask)) return 1u;
    return 0u;
}
uint8_t c3_exposure_verify_manual_night(const c3_exposure_t *s,c3_exposure_read_t rd)
{
    if(!s||!rd) return 0u;
    if(c3_exposure_effective_dial(s)==C3_EXPOSURE_AUTO_CODE) return 1u;
    return (uint8_t)(zoom_expect(rd,0x13u,s->shoot_mode==C3_SHOOT_S?4u:0u,5u) &&
                     zoom_expect(rd,0x0Eu,0u,0xFFu) && zoom_expect(rd,0x2Du,0u,0xFFu) &&
                     zoom_expect(rd,0x2Eu,0u,0xFFu) && zoom_expect(rd,0x33u,0u,0xFFu) &&
                     zoom_expect(rd,0x34u,0u,0xFFu));
}
uint8_t c3_zoom_hold_matches(const c3_zoom_hold_t *h,const c3_exposure_t *s)
{
    return (uint8_t)(h && s && h->source.valid &&
        h->dial==c3_exposure_effective_dial(s) && h->gain==c3_exposure_effective_gain(s) &&
        (s->shoot_mode==C3_SHOOT_M || h->ev==s->ev_half_steps));
}
static c3_timing_format_t zoom_format(c3_exposure_sensor_mode_t mode)
{
    return mode==C3_EXPOSURE_RAW?C3_TIMING_RAW8:
           mode==C3_EXPOSURE_BMP?C3_TIMING_RGB1X:C3_TIMING_RGB4X;
}
uint16_t c3_zoom_hold_rows(const c3_zoom_hold_t *h,c3_exposure_sensor_mode_t mode)
{
    c3_shutter_plan_t src,dst;
    if(!h||!h->source.valid||mode>C3_EXPOSURE_BMP ||
       !c3_shutter_plan(zoom_format(mode),h->dial,h->dial!=C3_EXPOSURE_AUTO_CODE,&dst)) return 0u;
    if(h->dial!=C3_EXPOSURE_AUTO_CODE) return dst.rows;
    if(!h->source.preview_rows||h->source.preview_rows>8191u ||
       !c3_shutter_plan(C3_TIMING_RGB1X,h->dial,0u,&src)) return 0u;
    src.rows=h->source.preview_rows;
    uint32_t ns=c3_shutter_exposure_ns(C3_TIMING_RGB1X,&src);
    uint32_t row_ns=c3_shutter_row_ns(zoom_format(mode),&dst);
    if(!ns||!row_ns) return 0u;
    uint32_t rows=(ns+row_ns/2u)/row_ns;
    if(!rows)rows=1u;
    return rows<=8191u?(uint16_t)rows:0u;
}
/* A failed attempt never publishes a partially valid lock. */
#define ZOOM_READ_FAIL(code) do { return 0u; } while(0)
static uint8_t zoom_read_frozen(c3_zoom_hold_t *h,const c3_exposure_t *s,
                                c3_exposure_read_t rd)
{
    c3_exposure_snapshot_t a,b;
    uint8_t r,g,bl;
    if(!c3_exposure_snapshot_read(&a,rd)) ZOOM_READ_FAIL(12u);
    if(!rd(0x02u,&r)||!rd(0x03u,&g)||!rd(0x01u,&bl)) ZOOM_READ_FAIL(13u);
    if(!c3_exposure_snapshot_read(&b,rd)) ZOOM_READ_FAIL(14u);
    if(a.preview_rows!=b.preview_rows||a.gain_register!=b.gain_register) ZOOM_READ_FAIL(15u);
    if(r<0x40u||g<0x40u||bl<0x40u||r==0xFFu||g==0xFFu||bl==0xFFu) ZOOM_READ_FAIL(16u);
    if(!zoom_expect(rd,0x02u,r,0xFFu)||!zoom_expect(rd,0x03u,g,0xFFu)||!zoom_expect(rd,0x01u,bl,0xFFu)) ZOOM_READ_FAIL(17u);
    c3_zoom_hold_t candidate={0};
    candidate.dial=c3_exposure_effective_dial(s);candidate.gain=c3_exposure_effective_gain(s);candidate.ev=s->ev_half_steps;
    candidate.source=a;candidate.red=r;candidate.green=g;candidate.blue=bl;
    if(candidate.gain)candidate.source.gain_register=c3_exposure_gain_reg(candidate.gain);
    if(!c3_zoom_hold_rows(&candidate,C3_EXPOSURE_PREVIEW)||!c3_zoom_hold_rows(&candidate,C3_EXPOSURE_RAW)||
       !c3_zoom_hold_rows(&candidate,C3_EXPOSURE_BMP)) ZOOM_READ_FAIL(18u);
    *h=candidate;
    return 1u;
}
uint8_t c3_zoom_hold_read_synced(c3_zoom_hold_t *h,const c3_exposure_t *s,
                          c3_exposure_read_t rd,c3_exposure_write_t wr,c3_zoom_wait_t wait_frame)
{
    if(!h)return 0u;
    *h=(c3_zoom_hold_t){0};
    if(!s||!rd||!wr)return 0u;
    /* Only called in 1X, with FIFO stopped. Freeze once, then wait for a
     * fresh complete frame before taking a coherent snapshot. Never
     * re-enable auto controls between attempts. Caller restores 1X on fail. */
    if(wr(0x13u,0xF8u)||!zoom_expect(rd,0x13u,0u,7u)) ZOOM_READ_FAIL(11u);
    for(unsigned attempt=0;attempt<(wait_frame?3u:1u);++attempt){
        if(wait_frame){
            if(!wait_frame()) ZOOM_READ_FAIL(19u);
            if(!zoom_expect(rd,0x13u,0u,7u)) ZOOM_READ_FAIL(11u);
        }
        if(zoom_read_frozen(h,s,rd))return 1u;
    }
    return 0u;
}
uint8_t c3_zoom_hold_read(c3_zoom_hold_t *h,const c3_exposure_t *s,
                          c3_exposure_read_t rd,c3_exposure_write_t wr)
{
    /* Historical register-only path, for existing tests/old clients. */
    return c3_zoom_hold_read_synced(h,s,rd,wr,0);
}
#undef ZOOM_READ_FAIL
uint8_t c3_zoom_hold_apply(const c3_zoom_hold_t *h,c3_exposure_sensor_mode_t mode,
                           c3_exposure_read_t rd,c3_exposure_write_t wr)
{
    uint16_t rows=c3_zoom_hold_rows(h,mode);
    c3_shutter_plan_t p;
    if(!rows||!rd||!wr||!c3_shutter_plan(zoom_format(mode),h->dial,h->dial!=C3_EXPOSURE_AUTO_CODE,&p))return 0u;
    const uint8_t base=mode==C3_EXPOSURE_RAW?0u:0xF8u;
    /* P long exposure: use fixed vertical dummy rows rather than restarting
     * automatic frame-rate control. Conservative active row lower bound
     * (240 RGB / 480 RAW) + 4 rows of guard; never add rows in S/M. */
    unsigned limit=mode==C3_EXPOSURE_RAW?476u:236u;
    uint16_t extra=(uint16_t)((h->dial==C3_EXPOSURE_AUTO_CODE&&rows>limit)?rows-limit:0u);
    for(unsigned attempt=0;attempt<2u;++attempt){
        uint8_t old;
        if(!clear_manual_night_rows(base,rd,wr)||!rd(0x2Au,&old)||old==0xFFu||(old&15u))continue;
        const uint8_t hi=(uint8_t)((p.dummy>>8u)<<4u);
        if(wr(0x2Au,hi)||wr(0x2Bu,(uint8_t)p.dummy)||wr(0x11u,p.clkrc)||
           wr(0x34u,(uint8_t)(extra>>8u))||wr(0x33u,(uint8_t)extra)||
           wr(0x1Fu,0u)||wr(0x08u,(uint8_t)(rows>>8u))||wr(0x10u,(uint8_t)rows)||
           wr(0x00u,h->source.gain_register))continue;
        if(mode!=C3_EXPOSURE_RAW && (wr(0x02u,h->red)||wr(0x03u,h->green)||wr(0x01u,h->blue)))continue;
        /* Readback includes the final state, not just values before apply. */
        if(!zoom_expect(rd,0x13u,0u,7u)||!zoom_expect(rd,0x0Eu,0u,0xFFu)||
           !zoom_expect(rd,0x2Du,0u,0xFFu)||!zoom_expect(rd,0x2Eu,0u,0xFFu)||
           !zoom_expect(rd,0x34u,(uint8_t)(extra>>8u),0xFFu)||!zoom_expect(rd,0x33u,(uint8_t)extra,0xFFu)||
           !zoom_expect(rd,0x2Au,hi,0xFFu)||!zoom_expect(rd,0x2Bu,(uint8_t)p.dummy,0xFFu)||
           !zoom_expect(rd,0x11u,p.clkrc,0x7Fu)||!zoom_expect(rd,0x08u,(uint8_t)(rows>>8u),0xFFu)||
           !zoom_expect(rd,0x10u,(uint8_t)rows,0xFFu)||!zoom_expect(rd,0x1Fu,0u,0xFFu)||
           !zoom_expect(rd,0x00u,h->source.gain_register,0xFFu))continue;
        if(mode!=C3_EXPOSURE_RAW&&(!zoom_expect(rd,0x02u,h->red,0xFFu)||
           !zoom_expect(rd,0x03u,h->green,0xFFu)||!zoom_expect(rd,0x01u,h->blue,0xFFu)))continue;
        return 1u;
    }
    return 0u;
}

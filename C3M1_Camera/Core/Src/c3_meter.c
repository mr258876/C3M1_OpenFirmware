/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_meter.h"
#include <stddef.h>

/* OV7725 Rev 1.4, Device Control Register List:
 * GAIN = 0x00; AECH = 0x08; AEC = 0x10.
 * The four upper GAIN bits each multiply gain by two; low nibble is
 * 1 + lower_nibble/16. Q4 gain therefore is (16+low) << popcount(high).
 */
uint16_t c3_meter_decode_gain_x16(uint8_t gain_reg)
{
    uint16_t result = (uint16_t)(16u + (gain_reg & 0x0Fu));
    for (uint8_t bit = 4u; bit < 8u; ++bit) {
        if (gain_reg & (uint8_t)(1u << bit)) result <<= 1u;
    }
    return result;
}

void c3_meter_reset(c3_meter_t *m)
{
    if (!m) return;
    m->exposure_rows = 0u;
    m->gain_x16 = 0u;
    m->last_attempt_ms = 0u;
    m->valid = 0u;
    m->attempted = 0u;
    m->warning_rows = 0u;
    m->warning_fine = 0u;
    m->warning_candidate = 0u;
    m->warning_samples = 0u;
    m->warning = C3_METER_WARN_NONE;
    m->raw_gain = m->yave = m->read_aeb = m->read_aew = 0u;
    m->read_com8 = m->read_com9 = m->read_flags = 0u;
    m->warning_reason = C3_METER_REASON_SAMPLE;
}

uint8_t c3_meter_poll(c3_meter_t *m, uint32_t now_ms,
                       c3_meter_read_t read_reg)
{
    if (!m || !read_reg) return 0u;
    if (m->attempted &&
        (uint32_t)(now_ms - m->last_attempt_ms) < C3_METER_SAMPLE_MS)
        return 0u;
    m->last_attempt_ms = now_ms;
    m->attempted = 1u;

    uint8_t hi0 = 0u, lo = 0u, hi1 = 0u, raw_gain = 0u;
    uint8_t coherent = 0u;
    /* An AEC update can occur between two SCCB transactions, especially
     * across low-byte carry. Re-read high and try once more if needed. */
    for (unsigned attempt = 0u; attempt < 2u; ++attempt) {
        if (!read_reg(0x08u, &hi0) || !read_reg(0x10u, &lo) ||
            !read_reg(0x08u, &hi1)) break;
        if (hi0 == hi1) { coherent = 1u; break; }
    }
    if (!coherent || !read_reg(0x00u, &raw_gain)) {
        m->valid = 0u; /* NEVER label stale samples as live data. */
        return 1u;
    }
    m->exposure_rows = (uint16_t)(((uint16_t)hi1 << 8u) | lo);
    m->raw_gain = raw_gain;
    m->gain_x16 = c3_meter_decode_gain_x16(raw_gain);
    m->valid = 1u;
    return 1u;
}

static void warning_clear(c3_meter_t *m)
{
    m->warning_candidate = C3_METER_WARN_NONE;
    m->warning_samples = 0u;
    m->warning = C3_METER_WARN_NONE;
}

static uint8_t warning_read_controls(c3_meter_t *m, uint8_t mode,
                                      uint8_t aeb, uint8_t aew,
                                      c3_meter_read_t rd)
{
    uint8_t ok=1u;
    /* Read all four, even if one fails, for actionable diagnostic output.
     * SCCB checked() is transport completion, not an ACK/data guarantee. */
    if (rd(0x13u,&m->read_com8)) m->read_flags |= 2u; else ok=0u;
    if (rd(0x14u,&m->read_com9)) m->read_flags |= 4u; else ok=0u;
    if (rd(0x25u,&m->read_aeb)) m->read_flags |= 8u; else ok=0u;
    if (rd(0x24u,&m->read_aew)) m->read_flags |= 16u; else ok=0u;
    return (uint8_t)(ok && (m->read_com8&5u)==(mode==0u?5u:4u) &&
        (m->read_com9&0xF0u)==0x30u && m->read_aeb==aeb && m->read_aew==aew);
}

void c3_meter_update_warning(c3_meter_t *m, uint8_t shoot_mode,
                             uint8_t aeb, uint8_t aew,
                             c3_meter_read_t read_reg)
{
    if (!m) return;
    uint8_t fine=0u, candidate=C3_METER_WARN_NONE;
    const uint16_t rows=m->exposure_rows, gain=m->gain_x16;
    m->read_flags=0u;
    m->warning_reason=C3_METER_REASON_SAMPLE;
    if (!m->valid || shoot_mode>1u || !read_reg || aeb>=aew ||
        rows>8191u || (shoot_mode==1u && rows==0u) ||
        gain<16u || gain>496u || !read_reg(0x2Fu,&m->yave)) {
        warning_clear(m);
        return;
    }
    m->read_flags=1u;
    uint8_t dark=(unsigned)m->yave+C3_METER_WARN_MARGIN<aeb;
    uint8_t bright=(unsigned)m->yave>(unsigned)aew+C3_METER_WARN_MARGIN;
    if (dark && gain>=C3_METER_GAIN_NEAR_MAX && (shoot_mode==1u || rows>1u))
        candidate=C3_METER_WARN_DARK;
    else if (bright && gain<=C3_METER_GAIN_NEAR_MIN && (shoot_mode==1u || rows<=1u))
        candidate=C3_METER_WARN_BRIGHT;

    /* Old global init warning flags include unrelated RGB matrix registers.
     * Validate the actual AEC/AGC configuration instead. Also reject the
     * common all-FF floating bus pattern. Read the control registers only if a warning is possible. */
    uint8_t config_ok=1u;
    if (candidate!=C3_METER_WARN_NONE)
        config_ok=warning_read_controls(m,shoot_mode,aeb,aew,read_reg);
    if (!config_ok) {
        m->warning_reason=C3_METER_REASON_CONFIG;
        warning_clear(m);
        return;
    }
    if (candidate==C3_METER_WARN_NONE) {
        m->warning_reason=dark||bright?C3_METER_REASON_GAIN:C3_METER_REASON_NORMAL;
        warning_clear(m);
        return;
    }
    if (candidate==C3_METER_WARN_BRIGHT && shoot_mode==0u &&
        !read_reg(0x1Fu,&fine)) {
        m->warning_reason=C3_METER_REASON_FINE;
        warning_clear(m);
        return;
    }
    uint16_t delta=rows>m->warning_rows?rows-m->warning_rows:m->warning_rows-rows;
    uint16_t tolerance=m->warning_rows/100u;
    if (tolerance<2u) tolerance=2u;
    uint8_t fine_delta=fine>m->warning_fine?fine-m->warning_fine:m->warning_fine-fine;
    /* Allow modest AEC dithering around the candidate's initial value,
     * while still rejecting a large continuing night-mode exposure ramp. */
    m->warning_reason=C3_METER_REASON_SETTLING;
    if (candidate!=m->warning_candidate ||
        (shoot_mode==0u && (candidate==C3_METER_WARN_DARK ? delta>tolerance :
                            (delta!=0u || fine_delta>1u)))) {
        m->warning_candidate=candidate;
        m->warning_rows=rows;
        m->warning_fine=fine;
        m->warning_samples=1u;
        m->warning=C3_METER_WARN_NONE;
        return;
    }
    if (m->warning_samples<C3_METER_WARN_SAMPLES) ++m->warning_samples;
    if (m->warning_samples>=C3_METER_WARN_SAMPLES) {
        m->warning=candidate;
        m->warning_reason=C3_METER_REASON_ACTIVE;
    }
}

static unsigned put_uint(char *dest, unsigned n)
{
    char reverse[6];
    unsigned len = 0u;
    do {
        reverse[len++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n && len < sizeof(reverse));
    for (unsigned i = 0u; i < len; ++i) dest[i] = reverse[len - i - 1u];
    return len;
}

void c3_meter_exposure_label(char out[12], uint16_t rows, uint8_t is_auto)
{
    if (!out) return;
    unsigned i = 0u;
    out[i++] = 'E';
    i += put_uint(out + i, rows);
    out[i++] = 'L';
    if (is_auto) out[i++] = 'A';
    out[i] = '\0';
}

void c3_meter_shutter_label(char out[12], uint16_t rows, uint32_t row_ns)
{
    if (!out) return;
    if (!rows || rows > 8191u || !row_ns || row_ns > 200000u) {
        out[0]='1'; out[1]='/'; out[2]='-'; out[3]='-'; out[4]='-';
        out[5]=0;
        return;
    }
    /* The C3M1 model assumes 12MHz output PCLK and includes RGB EXHCL.
     * For a live AEC readout use the actual sensor rows (not dial labels).
     * 8191 rows * 200000 ns fits in uint32_t. */
    uint32_t ns = (uint32_t)rows * row_ns;
    if (ns >= 1000000000u) {
        /* Long exposures do not fit the 1/x idiom. Show seconds instead. */
        uint32_t tenths=(ns + 50000000u)/100000000u;
        if (tenths > 99u) tenths=99u;
        unsigned i=put_uint(out,tenths/10u);
        out[i++]='.';
        out[i++]=(char)('0'+tenths%10u);
        out[i++]='S'; out[i]=0;
        return;
    }
    uint32_t denominator = (1000000000u + ns/2u)/ns;
    if (denominator < 1u) denominator=1u;
    if (denominator > 99999u) denominator=99999u;
    out[0]='1';out[1]='/';
    unsigned i=2u+put_uint(out+2u,denominator);
    out[i]=0;
}

void c3_meter_gain_label(char out[12], uint16_t gain_x16, uint8_t is_auto)
{
    if (!out) return;
    /* Gain is in Q4; rounded to 0.1x only for display. */
    unsigned gain_x10 = ((unsigned)gain_x16 * 10u + 8u) / 16u;
    unsigned i = 0u;
    out[i++] = 'G';
    i += put_uint(out + i, gain_x10 / 10u);
    out[i++] = '.';
    out[i++] = (char)('0' + gain_x10 % 10u);
    out[i++] = 'X';
    if (is_auto) out[i++] = 'A';
    out[i] = '\0';
}

/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* Pure byte-writing DNG 1.1-compatible TIFF IFD. No packed structs, no
   unspecified padding/endian/unaligned memory behavior. Host-unit-tested. */
#include "c3_dng.h"

/* v0.1.65: reverse only the experimental green-magenta AWB response,
 * while preserving the red/blue ratio (our first-order CCT coordinate). */
/* The 67.5% ColorMatrix1 was derived in D50 XYZ; the former D55 tag
 * described a different calibration illuminant. 0 permits exact rollback. */
/* The common-mode inverse uses a 32-bit fixed point product; reject
 * out-of-range custom neutral anchors rather than silently overflowing. */
_Static_assert((uint64_t)C3_DNG_NEUTRAL_R_PERMILLE *
               (uint64_t)C3_DNG_NEUTRAL_B_PERMILLE * 1000u <= UINT32_MAX,
               "DNG tint-reference product exceeds 32-bit range");

/* Per-shot metadata is kept until save succeeds or the user discards it;
 * storage retries reread the exact retained FIFO frame AND this same WB. */
static uint32_t captured_neutral[3] = {
    C3_DNG_NEUTRAL_R_PERMILLE * 100u,
    C3_DNG_NEUTRAL_G_PERMILLE * 100u,
    C3_DNG_NEUTRAL_B_PERMILLE * 100u
};
static uint8_t captured_wb_valid;
static uint8_t captured_sensor_awb[3]; /* R,G,B BEFORE tint remapping */
#include "c3_dng_black_lut.inc"
/* DNG BlackLevelRepeatDim is row/column, not RGB order. */
static uint16_t captured_black[4] = {1000u,1000u,1000u,1000u};
/* Nominal v177 RAW manual shutter integration in PCLK ticks, fastest first.
 * Manual dial selects a measured cell exactly. Auto RAW uses 784 ticks/row,
 * EXHCL=0, CLKRC=0; compare integration time, not manual AEC row count. */
static const uint32_t black_ticks[9] = {
    1500u,3000u,6000u,12000u,24000u,40000u,80000u,120000u,240000u
};
/* Positive weighted mean avoids signed division and overflow for this LUT. */
static uint16_t black_lerp(uint16_t a,uint16_t b,uint32_t n,uint32_t d)
{
    /* Maximum d=120000 and max black <16000: <=1.92e9, fits uint32. */
    return (uint16_t)(((uint32_t)a*(d-n)+(uint32_t)b*n+d/2u)/d);
}
static uint16_t black_at(unsigned g,uint8_t dial,uint16_t rows,unsigned c)
{
    if (dial<9u) return black_lut[g][dial][c];
    if (!rows || rows>8191u) {
        /* Unknown auto shutter: use the mean at known gain, not stale dial. */
        uint32_t sum=0u;
        for (unsigned i=0;i<9u;++i) sum+=black_lut[g][i][c];
        return (uint16_t)((sum+4u)/9u);
    }
    uint32_t ticks=(uint32_t)rows*784u;
    if (ticks<=black_ticks[0]) return black_lut[g][0][c];
    if (ticks>=black_ticks[8]) return black_lut[g][8][c];
    unsigned i=0u;
    while (ticks>black_ticks[i+1u]) ++i;
    return black_lerp(black_lut[g][i][c],black_lut[g][i+1u][c],
                      ticks-black_ticks[i],black_ticks[i+1u]-black_ticks[i]);
}
void c3_dng_set_black_level(uint8_t dial,uint8_t gain,uint16_t rows)
{
    /* 0xFF is ambiguous floating SCCB; do not treat it as a measured 31x. */
    if (gain==0xFFu) {
        for (unsigned c=0;c<4u;++c) captured_black[c]=1000u;
        return;
    }
    /* OV7725 GAIN: each high bit doubles, low nibble = fraction/16. */
    uint16_t gain16=(uint16_t)(16u+(gain&15u));
    for (unsigned bit=4u;bit<8u;++bit) if (gain&(1u<<bit)) gain16<<=1u;
    unsigned g=0u;
    uint16_t low=16u;
    while (g<4u && gain16>=(uint16_t)(low*2u)) {low*=2u;++g;}
    for (unsigned c=0;c<4u;++c) {
        uint16_t a=black_at(g,dial,rows,c);
        captured_black[c]=(g==4u || gain16==low)?a:
            black_lerp(a,black_at(g+1u,dial,rows,c),gain16-low,low);
    }
}
void c3_dng_capture_black_level(uint8_t dial,uint8_t commanded_gain,
                                uint16_t commanded_rows,c3_dng_reg_reader_t rd)
{
    uint8_t gain=commanded_gain;
    uint16_t rows=commanded_rows;
    if (rd) {
        uint8_t a,b;
        if (rd(0x00u,&a) && rd(0x00u,&b) && a==b && a!=0xFFu) gain=a;
        if (dial>=9u) {
            uint8_t hi0,lo0,hi1,lo1,hi2;
            if (rd(0x08u,&hi0) && rd(0x10u,&lo0) && rd(0x08u,&hi1) &&
                rd(0x10u,&lo1) && rd(0x08u,&hi2) &&
                hi0==hi1 && hi1==hi2 && lo0==lo1) {
                uint16_t v=(uint16_t)(((uint16_t)hi0<<8u)|lo0);
                if (v && v<=8191u) rows=v;
            }
        }
    }
    c3_dng_set_black_level(dial,gain,rows);
}

void c3_dng_begin_shot(void)
{
    for (unsigned c=0;c<4u;++c) captured_black[c]=1000u;
    captured_neutral[0] = C3_DNG_NEUTRAL_R_PERMILLE * 100u;
    captured_neutral[1] = C3_DNG_NEUTRAL_G_PERMILLE * 100u;
    captured_neutral[2] = C3_DNG_NEUTRAL_B_PERMILLE * 100u;
    captured_wb_valid = 0u;
    captured_sensor_awb[0]=captured_sensor_awb[1]=captured_sensor_awb[2]=0u;
}

uint8_t c3_dng_wb_sensor_valid(void) { return captured_wb_valid; }
uint8_t c3_dng_get_preview_awb_gains(uint8_t *red, uint8_t *green,
                                    uint8_t *blue)
{
    if (!captured_wb_valid || !red || !green || !blue) return 0u;
    *red=captured_sensor_awb[0];
    *green=captured_sensor_awb[1];
    *blue=captured_sensor_awb[2];
    return 1u;
}

uint8_t c3_dng_neutral_from_awb(uint8_t red, uint8_t green, uint8_t blue,
                                uint32_t out_per100k[3])
{
    if (!out_per100k) return 0u;
    /* Genuine 0xFF sensor saturation is indistinguishable from this board's
     * historical SCCB floating-line fault. Reject for safety. AWB gains are
     * usually >=1x; extremely low/unbalanced readbacks also fall back. */
    if (red < 0x40u || red == 0xFFu ||
        green < 0x40u || green == 0xFFu ||
        blue < 0x40u || blue == 0xFFu) return 0u;
    /* Unchanged power-up defaults / uniform stuck readbacks tell us nothing. */
    if ((uint8_t)(red == green) && green == blue) return 0u;
    uint32_t rr = ((uint32_t)green * 100000u + red / 2u) / red;
    uint32_t bb = ((uint32_t)green * 100000u + blue / 2u) / blue;
    if (rr < 50000u || rr > 200000u ||
        bb < 30000u || bb > 200000u) return 0u;
    /* Neutral R:G:B = (r,1,b).  The relative R/B ratio r/b controls the
     * first-order blue/red (temperature) coordinate; the product r*b
     * controls their common-mode level relative to green (tint).
     *
     * Mirror log(tint) about the fixed 5250K neutral (r0,b0), preserving
     * r/b exactly:  r'=r0*b0/b and b'=r0*b0/r.
     * Units are 1/100000. For Cortex-M3, scale the divisor by 10 to keep
     * the calculation in fast 32-bit arithmetic (error <0.02% here).
     * This is an experimental Camera Raw tint-polarity correction; no RAW
     * pixel is modified, and it does not assert a calibrated ACR slider.
     */
    const uint32_t ref_product =
        C3_DNG_NEUTRAL_R_PERMILLE * C3_DNG_NEUTRAL_B_PERMILLE * 1000u;
    const uint32_t den_r = (rr + 5u) / 10u;
    const uint32_t den_b = (bb + 5u) / 10u;
    if (!den_r || !den_b) return 0u;
    const uint32_t fixed_r = (ref_product + den_b / 2u) / den_b;
    const uint32_t fixed_b = (ref_product + den_r / 2u) / den_r;
    if (fixed_r < 50000u || fixed_r > 200000u ||
        fixed_b < 30000u || fixed_b > 200000u) return 0u;
    rr = fixed_r;
    bb = fixed_b;
    out_per100k[0] = rr;
    out_per100k[1] = 100000u;
    out_per100k[2] = bb;
    return 1u;
}

uint8_t c3_dng_capture_locked_awb(uint8_t r,uint8_t g,uint8_t b)
{
    c3_dng_begin_shot();
    uint32_t neutral[3];
    if(!c3_dng_neutral_from_awb(r,g,b,neutral))return 0u;
    for(unsigned i=0;i<3u;++i)captured_neutral[i]=neutral[i];
    captured_sensor_awb[0]=r;captured_sensor_awb[1]=g;captured_sensor_awb[2]=b;
    captured_wb_valid=1u;return 1u;
}

uint8_t c3_dng_capture_preview_awb(c3_dng_reg_reader_t read_reg)
{
    /* Always begin a new shot with the fixed, previously tested reference.
     * Call only while the live RGB565 preview is still configured. */
    c3_dng_begin_shot();
    if (!read_reg) return 0u;
    uint8_t com8a = 0u, com8b = 0u;
    uint8_t r0 = 0u, g0 = 0u, b0 = 0u, r1 = 0u, g1 = 0u, b1 = 0u;
    /* Full transactions can succeed yet return invalid 0x7F/0xFF, hence the
     * repeated values and conservative physical-ratio validation. */
    if (!read_reg(0x13u, &com8a) || !(com8a & 0x02u) ||
        !read_reg(0x02u, &r0) || !read_reg(0x03u, &g0) ||
        !read_reg(0x01u, &b0) || !read_reg(0x02u, &r1) ||
        !read_reg(0x03u, &g1) || !read_reg(0x01u, &b1) ||
        !read_reg(0x13u, &com8b) || com8a != com8b) return 0u;
    if ((r0 > r1 ? r0 - r1 : r1 - r0) > 8 ||
        (g0 > g1 ? g0 - g1 : g1 - g0) > 8 ||
        (b0 > b1 ? b0 - b1 : b1 - b0) > 8) return 0u;
    uint32_t neutral[3];
    if (!c3_dng_neutral_from_awb((uint8_t)(((uint16_t)r0 + r1) / 2u),
                                 (uint8_t)(((uint16_t)g0 + g1) / 2u),
                                 (uint8_t)(((uint16_t)b0 + b1) / 2u),
                                 neutral)) return 0u;
    captured_neutral[0] = neutral[0];
    captured_neutral[1] = neutral[1];
    captured_neutral[2] = neutral[2];
    captured_sensor_awb[0]=(uint8_t)(((uint16_t)r0 + r1) / 2u);
    captured_sensor_awb[1]=(uint8_t)(((uint16_t)g0 + g1) / 2u);
    captured_sensor_awb[2]=(uint8_t)(((uint16_t)b0 + b1) / 2u);
    captured_wb_valid = 1u;
    return 1u;
}


static void wr16(uint8_t *p, unsigned ofs, uint16_t v)
{
    p[ofs] = (uint8_t)v;
    p[ofs+1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, unsigned ofs, uint32_t v)
{
    wr16(p, ofs, (uint16_t)v);
    wr16(p, ofs+2, (uint16_t)(v >> 16));
}
static void ifd(uint8_t *p, unsigned idx, uint16_t tag, uint16_t type,
                uint32_t count, uint32_t value)
{
    unsigned at = 10u + idx*12u;
    wr16(p, at, tag);
    wr16(p, at+2u, type);
    wr32(p, at+4u, count);
    wr32(p, at+8u, value);
}
void c3_dng_make_header(uint8_t out[C3_DNG_HEADER_BYTES])
{
    /* Always keep exactly 512 bytes before the first RAW byte. The
     * v173 layout appends the camera model and profile; v179 additionally
     * appends the held neutral and per-CFA black level. Legacy layouts
     * retain their previous byte representation. */
    /* 28 entries need 350 bytes. Keep both matrices in the prefix; move
     * AsShotNeutral and new RATIONAL[4] BlackLevel to the appended tail.
     * The original 112-byte profile tail is untouched. */
    enum { TAGS = 28, MATRIX_OFS = 352, FORWARD_OFS = 424,
           TAIL_OFS = C3_DNG_HEADER_BYTES + C3_DNG_STRIP_BYTES,
           MODEL_OFS = TAIL_OFS, PROFILE_OFS = TAIL_OFS + 20,
           CURVE_OFS = TAIL_OFS + 40, NEUTRAL_OFS = TAIL_OFS + 112,
           BLACK_OFS = TAIL_OFS + 136 };
    _Static_assert(10u+TAGS*12u+4u <= MATRIX_OFS &&
                   MATRIX_OFS+72u <= FORWARD_OFS &&
                   FORWARD_OFS+72u <= C3_DNG_HEADER_BYTES,
                   "v179 IFD/matrices overlap");
    _Static_assert(BLACK_OFS+32u == TAIL_OFS+C3_DNG_TAIL_BYTES,
                   "v179 tail layout mismatch");
    static const char model[] = "C3M1 OV7725 RAW8";
    unsigned n=0;
    for (unsigned k=0;k<C3_DNG_HEADER_BYTES;k++) out[k]=0;
    out[0] = 'I'; out[1] = 'I'; wr16(out, 2, 42); wr32(out, 4, 8);
    wr16(out, 8, TAGS);

    /* All tags strictly ascending as required by common TIFF readers. */
    ifd(out,n++,254,4,1,0);                       /* NewSubfileType */
    ifd(out,n++,256,4,1,C3_RAW_WIDTH);            /* ImageWidth */
    ifd(out,n++,257,4,1,C3_RAW_HEIGHT);           /* ImageLength */
    ifd(out,n++,258,3,1,8u*C3_DNG_SAMPLE_BYTES);                       /* BitsPerSample */
    ifd(out,n++,259,3,1,1);                       /* Compression=none */
    ifd(out,n++,262,3,1,32803);                   /* CFA */
    ifd(out,n++,273,4,1,C3_DNG_HEADER_BYTES);     /* StripOffsets */
    ifd(out,n++,274,3,1,3);                       /* Orientation: 180 degrees */
    ifd(out,n++,277,3,1,1);                       /* SamplesPerPixel */
    ifd(out,n++,278,4,1,C3_RAW_HEIGHT);           /* RowsPerStrip */
    ifd(out,n++,279,4,1,C3_DNG_STRIP_BYTES);            /* StripByteCounts */
    ifd(out,n++,284,3,1,1);                       /* PlanarConfiguration */
    ifd(out,n++,33421,3,2,0x00020002u);          /* CFARepeatPatternDim 2x2 */
    /* Physically recorded, unrotated mosaic is BGGR (B G / G R).
     * Orientation=3 rotates DISPLAY only; CFAPattern describes stored RAW. */
    ifd(out,n++,33422,1,4,0x00010102u);          /* CFA pattern BGGR */
    ifd(out,n++,50706,1,4,0x00000401u);          /* DNGVersion=1.4 */
    ifd(out,n++,50707,1,4,0x00000101u);          /* DNGBackwardVersion=1.1 */
    ifd(out,n++,50708,2,sizeof(model),MODEL_OFS); /* UniqueCameraModel */
    ifd(out,n++,50713,3,2,0x00010001u);          /* E: 1x1 */
    ifd(out,n++,50714,5,1,BLACK_OFS);            /* E: scalar RATIONAL */
    ifd(out,n++,50717,4,1,255u*C3_DNG_SCALE);      /* scale with stored samples */
    ifd(out,n++,50721,10,9,MATRIX_OFS);          /* ColorMatrix1 SRATIONAL */
    ifd(out,n++,50728,5,3,NEUTRAL_OFS);         /* AsShotNeutral RATIONAL[3] */
    ifd(out,n++,50778,3,1,
        23u); /* D50: ColorMatrix1 and ForwardMatrix1 were derived in D50 XYZ */
    /* These tags reproduce the v0.1.72 host-generated M675_Soft.DNG.
     * Both the as-shot profile selector and embedded profile name point to
     * the same ASCII string; ProfileEmbedPolicy=0 permits profile copying. */
    ifd(out,n++,50934,1,sizeof("C3M1 Soft M675 v1"),PROFILE_OFS); /* AsShotProfileName */
    ifd(out,n++,50936,2,sizeof("C3M1 Soft M675 v1"),PROFILE_OFS); /* ProfileName */
    ifd(out,n++,50940,11,18,CURVE_OFS);            /* ProfileToneCurve FLOAT pairs */
    ifd(out,n++,50941,4,1,0u);                     /* ProfileEmbedPolicy 0 */
    ifd(out,n++,50964,10,9,FORWARD_OFS);       /* ForwardMatrix1 */

    /* Next IFD pointer naturally remains zero. */
    (void)model;  /* MODEL_OFS now points to the appended profile tail. */
    /* Each SRATIONAL is signed numerator/positive denominator (1e5).
     * DNG ColorMatrix1 is XYZ D50 -> *native* camera RGB (the inverse of
     * XYZ_D50_from_sRGB * M_67.5_percent * diag(1/neutral_5250K)).
     * ForwardMatrix1 is WB-camera RGB -> XYZ D50, i.e.
     * XYZ_D50_from_sRGB * M_67.5_percent. Both use row scan order.
     * A user-selected stylistic fit, NOT a certified chart calibration. */
    static const int32_t color[9] = {
       216326,-77936,-31623,
       -43403,135031, 8264,
       -11638,23588,49632
    };
    static const int32_t forward[9] = {
       55316,25386,15721,
       17495,84305,-1800,
       4656,-34114,111979
    };
    for (unsigned i=0; i<9; ++i) {
        wr32(out, MATRIX_OFS + 8u*i, (uint32_t)color[i]);
        wr32(out, MATRIX_OFS + 8u*i + 4u, 100000u);
        wr32(out, FORWARD_OFS + 8u*i, (uint32_t)forward[i]);
        wr32(out, FORWARD_OFS + 8u*i + 4u, 100000u);
    }
}

/* Exact IEEE-754 FLOAT bit patterns extracted from the M675_Soft DNG
 * tested in Camera Raw.  No Cortex-M3 floating-point operations required.
 * Coordinates: (0,0),(.10,.10),(.28,.28),(.45,.448),(.62,.602),
 *              (.75,.704),(.86,.806),(.93,.874),(1,1). */
void c3_dng_make_tail(uint8_t out[C3_DNG_TAIL_BYTES])
{
    static const char model[] = "C3M1 OV7725 RAW8";
    static const char profile[] = "C3M1 Soft M675 v1";
    static const uint32_t curve[18] = {
        0x00000000u, 0x00000000u, 0x3dcccccdu, 0x3dcccccdu,
        0x3e8f5c29u, 0x3e8f5c29u, 0x3ee66666u, 0x3ee56042u,
        0x3f1eb852u, 0x3f1a1cacu, 0x3f400000u, 0x3f343958u,
        0x3f5c28f6u, 0x3f4e5604u, 0x3f6e147bu, 0x3f5fbe77u,
        0x3f800000u, 0x3f800000u
    };
    _Static_assert(sizeof(model) <= 20u && sizeof(profile) <= 20u,
                   "Profile header strings exceed assigned tail slots");
    _Static_assert(40u + sizeof(curve) + (C3_DNG_BLACK_CALIBRATION ? 56u : 0u) == C3_DNG_TAIL_BYTES,
                   "Profile payload size mismatch");
    for (unsigned i=0;i<C3_DNG_TAIL_BYTES;++i) out[i]=0u;
    for (unsigned i=0;i<sizeof(model);++i) out[i]=(uint8_t)model[i];
    for (unsigned i=0;i<sizeof(profile);++i) out[20u+i]=(uint8_t)profile[i];
    for (unsigned i=0;i<18u;++i) wr32(out,40u+i*4u,curve[i]);
    for (unsigned i=0;i<3u;++i) {
        wr32(out,112u+8u*i,captured_neutral[i]);
        wr32(out,116u+8u*i,100000u);
    }
    const uint32_t sum=(uint32_t)captured_black[0]+captured_black[1]+
                       captured_black[2]+captured_black[3];
    wr32(out,136u,(sum+2u)/4u);
    wr32(out,140u,1000u);
}

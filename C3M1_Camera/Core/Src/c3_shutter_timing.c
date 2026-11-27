#include "c3_shutter_timing.h"
/* The 1X and RAW/VGA combinations come from the v175/v176 read-only
 * experiments. No LAEC: previous measurements found it unresponsive. */
uint8_t c3_shutter_plan(c3_timing_format_t fmt, uint8_t dial,
                        uint8_t manual, c3_shutter_plan_t *out)
{
    if (!out || fmt>C3_TIMING_RAW8) return 0u;
    c3_shutter_plan_t p={0};
    if (!manual) {
        /* Automatic exposure continues at the known-good original clock.
         * RAW has EXHCL=0; RGB 1X/4X baseline is EXHCL=0x80. */
        p.dummy=(fmt==C3_TIMING_RAW8)?0u:0x80u;
        p.rows=1u;  /* AEC remains controlled by the sensor. */
    } else if (dial>=9u) {
        return 0u;
    } else if (fmt==C3_TIMING_RGB1X) {
        p.dummy=(dial<=4u)?0xAEu:0x8Au;
        static const uint16_t rows[9]={1u,2u,4u,8u,16u,28u,56u,84u,168u};
        p.rows=rows[dial];
    } else if (fmt==C3_TIMING_RGB4X) {
        if (dial==0u) {
            /* Need the CLKRC[6] bypass to get 1/8000 at VGA geometry.
             * The v176 FAST variant must be field validated; opt-out exists
             * as C3_4X_FAST_8000=0 with 1/6579 limitation. */
#ifndef C3_4X_FAST_8000
#define C3_4X_FAST_8000 1
#endif
#if C3_4X_FAST_8000
            p.dummy=716u;p.rows=1u;p.clkrc=0x40u;
#else
            p.dummy=128u;p.rows=1u;
#endif
        } else if (dial==1u) {
            p.dummy=716u;p.rows=1u;
        } else {
            static const uint16_t rows[7]={3u,6u,12u,20u,40u,60u,120u};
            p.dummy=216u;p.rows=rows[dial-2u];
        }
    } else {
        if (dial==0u) {p.dummy=716u;p.rows=1u;}
        else {
            static const uint16_t rows[8]={3u,6u,12u,24u,40u,80u,120u,240u};
            p.dummy=216u;p.rows=rows[dial-1u];
        }
    }
    *out=p;
    return 1u;
}
uint32_t c3_shutter_row_ns(c3_timing_format_t fmt,
                           const c3_shutter_plan_t *p)
{
    if (!p || fmt>C3_TIMING_RAW8 || p->dummy>4095u ||
        (p->clkrc!=0u && p->clkrc!=0x40u)) return 0u;
    uint32_t ticks=(uint32_t)(fmt==C3_TIMING_RGB1X?576u:784u)+p->dummy;
    if (fmt!=C3_TIMING_RAW8) ticks*=2u;
    /* Bypass CLKRC[6] doubles inferred PCLK from 12 to 24 MHz. */
    uint32_t f=(p->clkrc==0x40u)?24u:12u;
    return (ticks*1000u+f/2u)/f;
}
uint32_t c3_shutter_exposure_ns(c3_timing_format_t fmt,
                                const c3_shutter_plan_t *p)
{
    if (!p || !p->rows || p->rows>8191u) return 0u;
    uint32_t row_ns=c3_shutter_row_ns(fmt,p);
    if (!row_ns) return 0u;
    /* Up to 8191*~500us fits in 32-bit ns? Some extreme combinations
     * exceed 32 bits; guard before multiply. */
    if (p->rows>UINT32_MAX/row_ns) return 0u;
    return (uint32_t)p->rows*row_ns;
}

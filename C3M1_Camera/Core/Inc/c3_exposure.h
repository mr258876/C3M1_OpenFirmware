#ifndef C3_EXPOSURE_H
#define C3_EXPOSURE_H
#include <stdint.h>
#include "c3_shutter_timing.h"
#ifndef C3_ZOOM_LOCK_DIAG
#define C3_ZOOM_LOCK_DIAG 0
#endif
#ifndef C3_ZOOM_AE_LOCK
#define C3_ZOOM_AE_LOCK 0
#endif
/* v177: mainline shutter time optimization; 0 restores v173 model/writes. */
#ifndef C3_SHUTTER_DYNAMIC_TIMING
#define C3_SHUTTER_DYNAMIC_TIMING 0
#endif
/* Freeze RGB preview AWB before switching to RAW8; no new RAW AWB loop. */
#ifndef C3_RAW_FREEZE_AWB
#define C3_RAW_FREEZE_AWB 1
#endif

/* v0.1.69: datasheet-timing candidate. 0 retains the exact pre-v169
 * integer-row table and the old CLKRC-only RAW inheritance conversion.
 * 12 MHz oscillator is populated on the C3M1 CMOS module; PLL4/CLKRC0
 * and 0x9E horizontal dummy pixels give a QVGA model of ~29.40 fps,
 * matching field observations. PCLK=12 MHz is an INFERRED sensor output
 * clock, not an independently probed frequency. Build-time override keeps
 * the assumption explicit if subsequent photographic validation disagrees.
 */
#ifndef C3_SHUTTER_DATASHEET_TIMING
#define C3_SHUTTER_DATASHEET_TIMING 1
#endif
#ifndef C3_SENSOR_PCLK_HZ
#define C3_SENSOR_PCLK_HZ 12000000u
#endif
#ifndef C3_RAW_CLKRC
#define C3_RAW_CLKRC 0u
#endif
#if C3_SHUTTER_DYNAMIC_TIMING && (!C3_SHUTTER_DATASHEET_TIMING || C3_SENSOR_PCLK_HZ != 12000000u || C3_RAW_CLKRC != 0u)
#error "v177 dynamic tables require 12MHz PCLK model, datasheet timing, RAW CLKRC=0"
#endif
#if C3_SHUTTER_DATASHEET_TIMING != 0 && C3_SHUTTER_DATASHEET_TIMING != 1
#error "C3_SHUTTER_DATASHEET_TIMING must be 0 or 1"
#endif
#if C3_SENSOR_PCLK_HZ < 4000000u || C3_SENSOR_PCLK_HZ > 24000000u
#error "C3_SENSOR_PCLK_HZ outside expected sensor PCLK range"
#endif

/* Production TSR-16: four separate 1/2/4/8 bit contacts on PF6..PF9.
 * The 0..8 detents have printed shutter targets; 9..F are unlabelled.
 * Detents 9..F select AUTO shutter and force AUTO gain (P).
 * Rows are provisional; verify the OV7725 line period in both formats.
 */
#define C3_EXPOSURE_DIAL_SETTLE_MS 60u
#define C3_EXPOSURE_GAIN_STEPS 6u
#define C3_EXPOSURE_DIAL_STEPS 16u
#define C3_EXPOSURE_DEFINED_STEPS 9u
#define C3_EXPOSURE_AUTO_CODE 0xFFu

typedef enum {
    C3_SHOOT_P = 0, /* auto exposure and auto gain */
    C3_SHOOT_S = 1, /* manual exposure rows; AGC always enabled */
    C3_SHOOT_M = 2  /* manual exposure rows and gain */
} c3_shoot_mode_t;

typedef struct {
    uint8_t dial_code;       /* debounced physical contact code 0..15 */
    uint8_t gain_step;       /* 0=auto, 1..5=1x/2x/4x/8x/16x */
    int8_t ev_half_steps;    /* -6..+6: nominal -3.0..+3.0 EV, 0.5 EV steps */
    c3_shoot_mode_t shoot_mode;
    uint8_t candidate;
    uint32_t candidate_since;
} c3_exposure_t;

typedef uint8_t (*c3_exposure_write_t)(uint8_t addr, uint8_t value);
typedef uint8_t (*c3_exposure_read_t)(uint8_t addr, uint8_t *value);
/* A shutter-time PREVIEW sensor register snapshot (not a metering estimate).
 * We intentionally retain the raw 8-bit GAIN register to avoid quantization. */
typedef struct {
    uint16_t preview_rows;
    uint8_t gain_register;
    uint8_t valid;
} c3_exposure_snapshot_t;
/* Refuse untrustworthy SCCB data; caller must fall back to legacy RAW AEC. */
uint8_t c3_exposure_snapshot_read(c3_exposure_snapshot_t *out,
                                  c3_exposure_read_t read_reg);
/* v169 model conversion: consider VGA/QVGA line length and RGB565 vs RAW
 * timing, not just CLKRC. C3_SHUTTER_DATASHEET_TIMING=0 preserves legacy. */
uint16_t c3_exposure_raw_rows(uint16_t preview_rows, uint8_t raw_clkrc);
/* Restore the AEC/GAIN last seen in preview before COM8 auto is enabled.
 * Manual modes will be explicitly applied afterwards. */
uint8_t c3_exposure_restore_preview_snapshot(const c3_exposure_snapshot_t *shot,
                                             const c3_exposure_t *exposure,
                                             c3_exposure_write_t write_reg);
uint8_t c3_exposure_apply_raw_locked(const c3_exposure_snapshot_t *shot,
                                     uint8_t raw_clkrc,
                                     c3_exposure_write_t wr);
/* v177: manual RAW uses its own calibrated AEC/EXHCL table; automatic RAW
 * continues preserving the actual shot-time preview AEC and sensor gain. */
uint8_t c3_exposure_apply_raw_locked_timed(const c3_exposure_snapshot_t *shot,
                                           const c3_exposure_t *state,
                                           uint8_t raw_clkrc,
                                           c3_exposure_write_t wr);
typedef enum {
    C3_EXPOSURE_PREVIEW = 0,
    C3_EXPOSURE_RAW = 1,
    /* Full-FoV BMP after sensor reinitialization, independent of 4x preview. */
    C3_EXPOSURE_BMP = 2
} c3_exposure_sensor_mode_t;

/* Input from GPIOF->IDR; bit0=PF6 TSR1, bit1=PF7 TSR2,
 * bit2=PF8 TSR4, bit3=PF9 TSR8. Contacts assert HIGH via a
 * resistor-pulled-up common pin; all four inputs use pull-downs. */
uint8_t c3_exposure_decode_gpio(uint16_t gpiof_idr);
uint16_t c3_exposure_target_denominator(uint8_t dial_code); /* 8000..50, 0 if undefined */
uint8_t c3_exposure_dial_defined(uint8_t code);
void c3_exposure_init(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms);
uint8_t c3_exposure_poll_dial(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms);
/* Reconcile actual dial position on return from menu/sleep. */
void c3_exposure_sync_dial(c3_exposure_t *s, uint8_t raw_dial, uint32_t now_ms);
/* ISO short press: cycle AUTO/1x/.../16x in S/M; no effect in P. */
void c3_exposure_gain_next(c3_exposure_t *s);
uint8_t c3_exposure_ev_adjust(c3_exposure_t *s, int steps); /* clamp at +/-3 EV */
uint8_t c3_exposure_ev_active(const c3_exposure_t *s); /* AEC or AGC automatic */
void c3_exposure_ev_label(char out[7], int8_t half_steps);
/* Writes AUTO target luminance region, not a guaranteed, calibrated EV amount. */
void c3_exposure_ev_targets(int8_t half_steps, uint8_t *aew, uint8_t *aeb, uint8_t *vpt);
/* Returns 0xFF only in P; defined 0..8 in S/M.
 * Dial codes 9..F select AUTO. */
uint8_t c3_exposure_effective_dial(const c3_exposure_t *s);
/* P and S report 0 (automatic AGC); M reports 1..5. */
uint8_t c3_exposure_effective_gain(const c3_exposure_t *s);
/* Tell exposure calculator whether live RGB565 sensor uses the 1x QVGA
 * window or the 4x VGA centre crop. For diagnostics default is 1x. */
void c3_exposure_set_preview_zoom(uint8_t zoom4x);
/* Predicted line period in nanoseconds; returns 0 for an invalid mode.
 * Only a timing MODEL; EXHCL and actual PCLK should be validated by photos. */
uint32_t c3_exposure_estimated_row_ns(c3_exposure_sensor_mode_t mode,
                                      uint8_t raw_clkrc);
/* Pure model used for OSD: unlike OpenMV's get_exposure_us(), include
 * RGB565 horizontal dummy pixels EXHCL. zoom4x selects VGA line timing.
 * No sensor read or GPIO side effects. */
uint32_t c3_exposure_preview_row_ns(uint8_t zoom4x);
/* OSD: use the current manual EXHCL and optional CLKRC bypass. */
uint32_t c3_exposure_preview_row_ns_dial(uint8_t zoom4x, uint8_t dial);
uint16_t c3_exposure_rows(uint8_t dial_code); /* preview rows, 0 for invalid */
uint16_t c3_exposure_rows_for_mode(uint8_t dial_code,
                                    c3_exposure_sensor_mode_t mode,
                                    uint8_t raw_clkrc);
uint8_t c3_exposure_gain_reg(uint8_t step);
/* Before applying AEC/GAIN, program 12-bit dummy pixel count and CLKRC
 * with COM8 auto controls stopped; preserve EXHCH output-size bits and
 * REQUIRE coherent readback (unlike SCCB's non-ACK ninth bit). This must
 * be called while FIFO capture is stopped, before c3_exposure_apply().
 * Manual shutter additionally disables COM5 and verifies vertical dummy
 * rows are zero before applying the calibrated horizontal timing. */
uint8_t c3_exposure_prepare_timing(const c3_exposure_t *s,
                                    c3_exposure_sensor_mode_t mode,
                                    c3_exposure_read_t rd,
                                    c3_exposure_write_t wr);
uint8_t c3_exposure_apply(const c3_exposure_t *s,
                          c3_exposure_sensor_mode_t mode,
                          c3_exposure_write_t write_reg);
#if C3_ZOOM_AE_LOCK
/* Holds the pre-zoom 1X exposure and actual ISP WB, not DNG neutrals.
 * Lifetime: one magnification session; never persisted in Flash. */
typedef struct {
    c3_exposure_snapshot_t source;
    uint8_t dial, gain, red, green, blue;
    int8_t ev;
    uint8_t warning;
} c3_zoom_hold_t;
#if C3_ZOOM_LOCK_DIAG
extern uint8_t c3_zoom_read_failure;
#endif
uint8_t c3_zoom_hold_matches(const c3_zoom_hold_t *h,const c3_exposure_t *s);
/* Wait callback returns success only after a fresh complete sensor frame.
 * With a callback: freeze once, at most 3 validated snapshots, no stale lock. */
typedef uint8_t (*c3_zoom_wait_t)(void);
uint8_t c3_zoom_hold_read_synced(c3_zoom_hold_t *h,const c3_exposure_t *s,
                          c3_exposure_read_t rd,c3_exposure_write_t wr,c3_zoom_wait_t wait_frame);
uint8_t c3_zoom_hold_read(c3_zoom_hold_t *h,const c3_exposure_t *s,
                          c3_exposure_read_t rd,c3_exposure_write_t wr);
uint16_t c3_zoom_hold_rows(const c3_zoom_hold_t *h,c3_exposure_sensor_mode_t mode);
uint8_t c3_zoom_hold_apply(const c3_zoom_hold_t *h,c3_exposure_sensor_mode_t mode,
                           c3_exposure_read_t rd,c3_exposure_write_t wr);
uint8_t c3_exposure_verify_manual_night(const c3_exposure_t *s,
                                        c3_exposure_read_t rd);
#endif
#endif

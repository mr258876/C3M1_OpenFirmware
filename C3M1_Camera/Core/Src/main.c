/* C3M1 OpenFirmware v0.1.93 - FAST+SP22 and FIFO preview row experiments.
 * Targets the production STM32F103ZET6/OV7725/AL422B/ST7789V3 board.
 * Full preview/capture/SD/settings code is enabled by default; hardware
 * validation remains required before installing on a working camera.
 */
#include "main.h"
#include "gpio.h"
#include "fsmc.h"
#include "sdio.h"
#include "fatfs.h"
#include "c3_camera.h"
#include "c3_lcd.h"
#include "c3_i18n.h"
#include "c3_ui_hints.h"
#include "c3_storage.h"
#include "c3_dng.h"
#include "c3_capture_recovery.h"
#include "c3_focus.h"
#include "c3_fps.h"
#include "c3_buttons.h"
#include "c3_input.h"
#include "c3_board_pins.h"
#include "c3_ui_nav.h"
#include "c3_menu.h"
#include "c3_playback.h"
#if C3_POSTSHOT
#include "c3_postshot.h"
#if !C3_PLAYBACK_ENABLE || !C3_MENU_ACTIONS || !C3_I18N || !C3_UI_ICONS
#error "Post-shot review requires normal bilingual playback build"
#endif
#endif
#include "c3_osd.h"
#include "c3_battery.h"
#include "c3_sleep.h"
#include "c3_settings.h"
#include "c3_exposure.h"
#include "c3_boot_timing.h"
#if C3_STARTUP_HINT
#include "c3_startup_hint.h"
#if C3_FIFO_DMA_LIVE_ROWS
extern volatile uint32_t c3_live_errors;
#endif
#endif
#ifndef C3_FAST_BMP_ENABLE
#define C3_FAST_BMP_ENABLE 1
#endif
/* Default ON: exposure/gain menu changes should not reset the sensor. */
#ifndef C3_FAST_PREVIEW_CONTROL
#define C3_FAST_PREVIEW_CONTROL 1
#endif
/* Experimental opt-in: change RGB565 -> RAW8 by differential register writes.
 * Always retain the original full-reset path as an automatic fallback. */
#ifndef C3_FAST_RAW_SWITCH
#define C3_FAST_RAW_SWITCH 0
#endif
#ifndef C3_FAST_RGB_RESTORE
#define C3_FAST_RGB_RESTORE 0
#endif
/* A short confirmation is enough; RAW sensor restore already leaves the
 * SAVED message on LCD until the next preview frame completes. */
#ifndef C3_SAVED_HOLD_MS
#define C3_SAVED_HOLD_MS 100u
#endif
#ifndef C3_RAW_LOCK_EXPOSURE
#define C3_RAW_LOCK_EXPOSURE 1
#endif
#if C3_FAST_RGB_RESTORE && (!C3_FAST_RAW_SWITCH || !C3_RAW_LOCK_EXPOSURE)
#error "Fast RGB restore requires fast RAW and an exposure snapshot"
#endif
#ifndef C3_RAW_CLKRC
#define C3_RAW_CLKRC 0
#endif
#ifndef C3_DNG_SETTLE_LOCKED
#define C3_DNG_SETTLE_LOCKED 2u
#endif
/* v180: preserve the calibrated baseline and make short waits explicit
 * experiments. Old build wrappers keep the legacy FIFO discard behavior. */
#ifndef C3_DNG_VSYNC_SETTLE
#define C3_DNG_VSYNC_SETTLE 0
#endif
#ifndef C3_DNG_FAST_SETTLE_FRAMES
#define C3_DNG_FAST_SETTLE_FRAMES 6u
#endif
#if C3_DNG_VSYNC_SETTLE && (C3_DNG_SETTLE_LOCKED != 6u || \
    (C3_DNG_FAST_SETTLE_FRAMES != 1u && C3_DNG_FAST_SETTLE_FRAMES != 2u && \
     C3_DNG_FAST_SETTLE_FRAMES != 3u && C3_DNG_FAST_SETTLE_FRAMES != 6u))
#error "VSYNC settling requires a six-frame fallback and fast 1/2/3/6 frames"
#endif

#include "c3_meter.h"
#include "c3_fifo_probe.h"
#include "c3_fifo_dma_probe.h"
#if C3_SCCB_SCAN_DIAG
#include "c3_sccb_diag.h"
#endif
#include "../../Drivers/BSP/OV7725/ov7725.h"
#include "../../Drivers/BSP/OV7725/sccb.h"
#include "../../Drivers/SYSTEM/delay/delay.h"

/* Delay from configuring LCD /RESET LOW to the first display
 * initialization. 5000 ms succeeded on one real cold boot; default
 * 1000 ms here was confirmed by cold-boot tests on this camera. */
#ifndef C3_SCCB_SCAN_DIAG
#define C3_SCCB_SCAN_DIAG 0
#endif
#if C3_SCCB_SCAN_DIAG && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE)
#error "SCCB scan is only for read-only preview diagnostic builds"
#endif
#ifndef C3_LCD_POWER_SETTLE_MS
#define C3_LCD_POWER_SETTLE_MS 1000u
#endif
/* v206 opt-in; historical builds retain the serial delay. */
#ifndef C3_CAM_POWER_SETTLE_MS
#define C3_CAM_POWER_SETTLE_MS 950u
#endif
#ifndef C3_BOOT_OVERLAP
#define C3_BOOT_OVERLAP 0
#endif
#ifndef C3_BOOT_DIAG
#define C3_BOOT_DIAG 0
#endif

/* Full mainline by default. Set C3_PREVIEW_PROBE=1 only for the
 * read-only sensor/FIFO diagnostic (no SD, still capture or Flash writes). */
#ifndef C3_PREVIEW_PROBE
#define C3_PREVIEW_PROBE 0
#endif
#ifndef C3_FIFO_PROBE
#define C3_FIFO_PROBE 1
#endif
/* v0.1.46: 20us shows intermittent SCCB FF reads on hardware.
 * Use 50us for normal mainline; 100us for a slower controlled A/B.
 * This remains a per-edge delay, and does not affect pixel streaming. */
#ifndef C3_SCCB_MAIN_HALF_US
#define C3_SCCB_MAIN_HALF_US 50u
#endif
/* Debug-only direct LCD color swatches (not camera/FIFO pixels).
 * Red/Green/Blue/White/Yellow across centre; compare their colors against
 * scene. If swatches are correct, color corruption is upstream of LCD. */
#ifndef C3_COLOR_SWATCH_DIAG
#define C3_COLOR_SWATCH_DIAG 0
#endif
#ifndef C3_LCD_COLOR_DIAG
#define C3_LCD_COLOR_DIAG 0
#endif
/* Read-only performance HUD. Never enable in the save-capable mainline. */
#ifndef C3_PROFILE_HUD
#define C3_PROFILE_HUD 0
#endif
#if C3_PROFILE_HUD && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE || !C3_PREVIEW_PROFILE)
#error "C3_PROFILE_HUD requires a read-only preview with DWT profiling"
#endif
#ifndef C3_CAM_COM3_RGB
#define C3_CAM_COM3_RGB 0xD0u
#endif
#if C3_LCD_COLOR_DIAG && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE || !C3_COLOR_SWATCH_DIAG)
#error "LCD color diagnostic requires read-only live preview with swatches"
#endif


/* Actual production schematic: PF1 ISO, PF2 SHOT, PF3 4X, PF10 FILM. */
#define BUTTONS C3_BTN_MASK

void SystemClock_Config(void);
/* Watch c3_boot_stage through SWD if a full-black boot ever recurs.
 * 1=GPIO, 2=LCD ready, 3=CAM init, 4=CAM done, 5=SD init, 6=loop. */
volatile uint32_t c3_boot_stage;
volatile uint32_t c3_cam_init_first_code;
#if C3_BOOT_DIAG
/* Times since HAL_Init, not since the physical power switch. */
static uint32_t boot_origin_ms,boot_lcd_ms,boot_cam_ms,boot_sd_ms,boot_first_ms;
static uint8_t boot_first_seen,boot_cam_ok,boot_sd_ok;
#if C3_FIFO_DMA_LIVE_ROWS
extern volatile uint32_t c3_live_errors;
#endif
static void boot_digits4(char *p,uint32_t n)
{
    if(n>9999u){p[0]=p[1]=p[2]=p[3]='+';return;}
    for(int i=3;i>=0;--i){p[i]=(char)('0'+n%10u);n/=10u;}
}
static void boot_show_timing(void)
{
    char a[]="LCD0000 CAM0000 SD0000";
    char b[]="FIRST0000 C0 S0";
    boot_digits4(a+3,boot_lcd_ms);
    boot_digits4(a+11,boot_cam_ms);
    boot_digits4(a+18,boot_sd_ms);
    boot_digits4(b+5,boot_first_ms);
    b[11]=(char)('0'+boot_cam_ok);
    b[14]=(char)('0'+boot_sd_ok);
    c3_lcd_rect(5,28,144,23,0x0000u);
    c3_lcd_text(8,30,a,0xFFE0u,1);
    c3_lcd_text(8,41,b,0xFFE0u,1);
}
#endif
static char c3_hex_digit(uint8_t b)
{
    b &= 0xFu;
    return (char)(b < 10u ? ('0'+b) : ('A'+b-10u));
}
static void c3_hex4(uint16_t v, char out[5])
{
    for (int i=3; i>=0; --i) { out[i] = c3_hex_digit((uint8_t)v); v >>= 4; }
    out[4] = 0;
}
#if C3_METER_DIAG
static void c3_diag_hex_byte(char *out,uint8_t v)
{
    out[0]=c3_hex_digit(v>>4);out[1]=c3_hex_digit(v);
}
static void c3_diag_optional_byte(char *out,uint8_t v,uint8_t ok)
{
    if (ok) c3_diag_hex_byte(out,v); else {out[0]='-';out[1]='-';}
}
static void c3_show_meter_diag(const c3_meter_t *m)
{
    char line1[]="Y -- G -- A ---- L -- H --";
    char line2[]="C8 -- C9 -- N - R - W -";
    char hex[5];
    c3_diag_optional_byte(line1+2,m->yave,m->read_flags&1u);
    c3_diag_optional_byte(line1+7,m->raw_gain,m->valid);
    if (m->valid) {c3_hex4(m->exposure_rows,hex);for(unsigned i=0;i<4u;++i)line1[12+i]=hex[i];}
    c3_diag_optional_byte(line1+19,m->read_aeb,m->read_flags&8u);
    c3_diag_optional_byte(line1+24,m->read_aew,m->read_flags&16u);
    c3_diag_optional_byte(line2+3,m->read_com8,m->read_flags&2u);
    c3_diag_optional_byte(line2+9,m->read_com9,m->read_flags&4u);
    line2[14]=c3_hex_digit(m->warning_samples);
    line2[18]=c3_hex_digit(m->warning_reason);
    line2[22]=c3_hex_digit(m->warning);
    c3_lcd_rect(6,174,166,23,0x0000u);
    c3_lcd_text(8,176,line1,0xFFE0u,1);
    c3_lcd_text(8,187,line2,0xFFE0u,1);
}
/* DIAG only: sample actual vertical timing independently of the meter;
 * also visible in M. Cache the text so SCCB isn't polled on every frame. */
static void c3_show_night_diag(uint8_t mode,uint32_t now
#if C3_ZOOM_AE_LOCK
                              ,uint8_t zoom
#endif
                              )
{
#if C3_ZOOM_AE_LOCK
    static char line[]="C5 -- ADV ---- DM ---- C8 -- V---";
    static uint8_t last_zoom=0xFFu;
    static uint32_t last_edges,last_stamp;
#else
    static char line[]="C5 -- ADV ---- DM ----";
#endif
    static uint8_t last_mode=0xFFu;
    static uint32_t last_ms;
    if (last_mode!=mode || (uint32_t)(now-last_ms)>=350u
#if C3_ZOOM_AE_LOCK
        || last_zoom!=zoom
#endif
        ) {
        uint8_t v=0xFFu;
        uint8_t ok=ov7725_read_reg_checked(0x0Eu,&v);
        c3_diag_optional_byte(line+3,v,ok);
        ok=ov7725_read_reg_checked(0x2Eu,&v);
        c3_diag_optional_byte(line+10,v,ok);
        ok=ov7725_read_reg_checked(0x2Du,&v);
        c3_diag_optional_byte(line+12,v,ok);
        ok=ov7725_read_reg_checked(0x34u,&v);
        c3_diag_optional_byte(line+18,v,ok);
        ok=ov7725_read_reg_checked(0x33u,&v);
        c3_diag_optional_byte(line+20,v,ok);
#if C3_ZOOM_AE_LOCK
        ok=ov7725_read_reg_checked(0x13u,&v);
        c3_diag_optional_byte(line+26,v,ok);
        /* Average physical VSYNC period over the last sample interval,
         * not the displayed frame rate. No timer/ISR changes. */
        uint32_t edges,stamp,e2;
        do{edges=c3_camera_vsync_edges;stamp=c3_camera_vsync_last_ms;e2=c3_camera_vsync_edges;}while(edges!=e2);
        uint32_t n=edges-last_edges;
        if(last_mode==mode && last_zoom==zoom && n && n<1000u){
            uint32_t period=(stamp-last_stamp+n/2u)/n;
            if(period>999u)period=999u;
            line[30]=(char)('0'+period/100u);line[31]=(char)('0'+period/10u%10u);line[32]=(char)('0'+period%10u);
        }else{line[30]=line[31]=line[32]='-';}
        last_edges=edges;last_stamp=stamp;last_zoom=zoom;
#endif
        last_mode=mode;last_ms=now;
    }
#if C3_ZOOM_AE_LOCK
    c3_lcd_rect(6,198,208,11,0x0000u);
#else
    c3_lcd_rect(6,198,166,11,0x0000u);
#endif
    c3_lcd_text(8,199,line,0xFFE0u,1);
}
#endif
static void show_sensor_detail(void)
{
    /* These are actual readbacks for the last OV7725 init attempt. */
    char txt[] = "ERR 0 MID 0000 PID 0000";
    char h[5];
    txt[4] = c3_hex_digit((uint8_t)c3_camera_last_sensor_code);
    c3_hex4((uint16_t)c3_ov_last_mid,h);
    for(unsigned i=0;i<4;i++) txt[10+i]=h[i];
    c3_hex4((uint16_t)c3_ov_last_pid,h);
    for(unsigned i=0;i<4;i++) txt[19+i]=h[i];
    c3_lcd_rect(8,144,303,82,0x0000u);
    c3_lcd_text(9,148,txt,0xFFE0u,1);
    char r[] = "REG 00 PH 0 TRY1 0";
    r[4]=c3_hex_digit((uint8_t)(c3_ov_last_bad_reg>>4));
    r[5]=c3_hex_digit((uint8_t)c3_ov_last_bad_reg);
    r[10]=c3_hex_digit((uint8_t)c3_ov_last_bad_phase);
    r[17]=c3_hex_digit((uint8_t)c3_cam_init_first_code);
    c3_lcd_text(9,170,r,0xFFFFu,1);
    if (c3_camera_last_sensor_code == 4u) {
        char verify[] = "VERIFY EXP 00 GOT 00";
        verify[11] = c3_hex_digit((uint8_t)(c3_ov_last_verify_expected >> 4));
        verify[12] = c3_hex_digit((uint8_t)c3_ov_last_verify_expected);
        verify[18] = c3_hex_digit((uint8_t)(c3_ov_last_verify_actual >> 4));
        verify[19] = c3_hex_digit((uint8_t)c3_ov_last_verify_actual);
        c3_lcd_text(9,190,verify,0xF800u,1);
    }
}

/* Show the EXACT still-initialization failing stage, rather than one
 * generic SENSOR ERROR. Nothing is written to SD before this succeeds. */
static void show_capture_init_error(c3_output_format_t format,
                                    uint8_t sensor_ready)
{
    c3_lcd_rect(8, 73, 304, 153, 0x0000u);
    c3_ui_text(12, 82, sensor_ready ? "EXPOSURE APPLY ERR" :
                (format == C3_OUTPUT_DNG ? "DNG SENSOR INIT ERR" :
                                           "BMP SENSOR INIT ERR"), 0xF800u, 1);
    if (sensor_ready) {
        c3_ui_text(12, 104, "CHECK EXPOSURE STATE", 0xFFE0u, 1);
    } else {
        char step[] = "STAGE 0 CODE 0";
        step[6] = c3_hex_digit((uint8_t)c3_camera_last_still_stage);
        step[13] = c3_hex_digit((uint8_t)c3_camera_last_still_code);
        c3_lcd_text(12, 104, step, 0xFFE0u, 1);
        if (c3_camera_last_still_code == 4u) {
            char verify[] = "REG 00 EXP 00 GOT 00";
            verify[4] = c3_hex_digit((uint8_t)(c3_ov_last_bad_reg >> 4));
            verify[5] = c3_hex_digit((uint8_t)c3_ov_last_bad_reg);
            verify[11] = c3_hex_digit((uint8_t)(c3_ov_last_verify_expected >> 4));
            verify[12] = c3_hex_digit((uint8_t)c3_ov_last_verify_expected);
            verify[18] = c3_hex_digit((uint8_t)(c3_ov_last_verify_actual >> 4));
            verify[19] = c3_hex_digit((uint8_t)c3_ov_last_verify_actual);
            c3_lcd_text(12, 126, verify, 0xFFE0u, 1);
        }
    }
    c3_ui_text(12, 159, "NO FRAME SAVED", 0xFFFFu, 1);
    c3_ui_text(12, 181, "REPORT CODE / REG / GOT", 0xFFFFu, 1);
}

/* SCCB read-speed A/B/C test: one power-up, identical RCC/GPIO/power,
 * same OV7725 reset. These are observations, NOT proof of correct writes.
 * Executed only in the read-only Preview Probe build. */
#if C3_PREVIEW_PROBE
volatile uint32_t c3_sccb_pid_good_5us = 0u;
volatile uint32_t c3_sccb_pid_good_20us = 0u;
volatile uint32_t c3_sccb_pid_good_50us = 0u;
volatile uint32_t c3_sccb_pid_last_5us = 0xFFFFu;
volatile uint32_t c3_sccb_pid_last_20us = 0xFFFFu;
volatile uint32_t c3_sccb_pid_last_50us = 0xFFFFu;

static void c3_sccb_probe_one(uint32_t us, volatile uint32_t *good,
                              volatile uint32_t *last)
{
    c3_sccb_half_period_us = us;
    sccb_init(); /* restore SDA/SCL idle before each set */
    HAL_Delay(5u);
    *good = 0u;
    *last = 0xFFFFu;
    for (uint8_t i = 0u; i < 16u; ++i) {
        uint16_t pid = (uint16_t)((uint16_t)ov7725_read_reg(0x0Au) << 8);
        pid |= ov7725_read_reg(0x0Bu);
        *last = pid;
        if (pid == OV7725_PID) ++(*good);
        HAL_Delay(2u);
    }
}

static void c3_sccb_probe_three_speeds(void)
{
    c3_sccb_probe_one(5u, &c3_sccb_pid_good_5us, &c3_sccb_pid_last_5us);
    c3_sccb_probe_one(20u, &c3_sccb_pid_good_20us, &c3_sccb_pid_last_20us);
    c3_sccb_probe_one(50u, &c3_sccb_pid_good_50us, &c3_sccb_pid_last_50us);
    c3_sccb_half_period_us = 50u; /* conservative init for this probe */
}

static void c3_sccb_show_probe_counts(void)
{
    char txt[] = "PID 5/20/50: 00 00 00";
    txt[13] = c3_hex_digit((uint8_t)(c3_sccb_pid_good_5us >> 4));
    txt[14] = c3_hex_digit((uint8_t)c3_sccb_pid_good_5us);
    txt[16] = c3_hex_digit((uint8_t)(c3_sccb_pid_good_20us >> 4));
    txt[17] = c3_hex_digit((uint8_t)c3_sccb_pid_good_20us);
    txt[19] = c3_hex_digit((uint8_t)(c3_sccb_pid_good_50us >> 4));
    txt[20] = c3_hex_digit((uint8_t)c3_sccb_pid_good_50us);
    c3_lcd_rect(7,203,306,34,0x0000u);
    c3_lcd_text(9,205,txt,0xFFFFu,1);
    char last[] = "LAST 50US 0000";
    last[10] = c3_hex_digit((uint8_t)(c3_sccb_pid_last_50us >> 12));
    last[11] = c3_hex_digit((uint8_t)(c3_sccb_pid_last_50us >> 8));
    last[12] = c3_hex_digit((uint8_t)(c3_sccb_pid_last_50us >> 4));
    last[13] = c3_hex_digit((uint8_t)c3_sccb_pid_last_50us);
    c3_lcd_text(9,221,last,0xFFE0u,1);
}
#endif

static void show_camera_error(void)
{
    switch (c3_camera_last_init_stage) {
        case 1u:
            c3_ui_message(c3_camera_last_sensor_code == 4u ?
                           "CAM VERIFY ERR" : "CAM SENSOR ERR");
            show_sensor_detail();
#if C3_PREVIEW_PROBE
            c3_sccb_show_probe_counts();
#endif
            break;
        case 2u: c3_ui_message("CAM WINDOW ERR"); break;
        case 3u: c3_ui_message("CAM EXPOSURE ERR"); break;
        case 4u: c3_ui_message("CAM VSYNC ERR"); break;
        default: c3_ui_message("CAM ERROR"); break;
    }
}

/* Active-high (pressed) mask, isolated from the debouncer for host tests. */
/* TSR-16 real 4-bit code: PF6 TSR1, PF7 TSR2, PF8 TSR4, PF9 TSR8.
 * Original factory UI displays 00..0F; physical markings cover 00..08. */
static uint8_t read_shutter_dial(void)
{
    return c3_exposure_decode_gpio((uint16_t)GPIOF->IDR);
}
static uint16_t read_buttons(void)
{
    return (uint16_t)(~GPIOF->IDR) & BUTTONS;
}
#if C3_LANGUAGE_SETUP
/* A static modal, called before camera/SD initialization on first boot,
 * or while the normal settings menu has already stopped preview. There
 * is intentionally no inactivity sleep or capture path in this loop. */
static uint8_t choose_language(c3_settings_t *settings, uint8_t required)
{
    c3_ui_nav_t picker;
    c3_ui_nav_language(&picker,read_shutter_dial(),HAL_GetTick(),
                       c3_settings_language(settings),required);
    c3_input_reset(read_buttons(),HAL_GetTick());
    c3_menu_draw_language(picker.language_choice,required,0u);
    uint8_t save_error=0u;
    for (;;) {
        c3_input_batch_t batch=c3_input_take();
        uint16_t action=c3_ui_nav_poll(&picker,batch.events,batch.stable,
                                       read_shutter_dial(),HAL_GetTick(),settings);
        if (action & C3_NAV_CONFIRM_CANCEL) {
            c3_input_reset(read_buttons(),HAL_GetTick());
            return 0u;
        }
        if (action & C3_NAV_LANGUAGE_CONFIRM) {
            if(c3_settings_flash_confirm_language(settings,picker.language_choice)) {
                c3_i18n_set_language(c3_settings_language(settings));
                c3_input_reset(read_buttons(),HAL_GetTick());
                return 1u;
            }
            save_error=1u;
            picker.confirm_armed=0u;
            c3_input_reset(read_buttons(),HAL_GetTick());
            c3_menu_draw_language(picker.language_choice,required,save_error);
        } else if (action & C3_NAV_CURSOR) {
            c3_menu_draw_language(picker.language_choice,required,save_error);
        }
        HAL_Delay(10u);
    }
}
#endif
#if C3_COLOR_SWATCH_DIAG
static void c3_show_lcd_color_reference(void)
{
    const uint16_t color[5] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0xFFE0u};
    const char *name[5] = {"R", "G", "B", "W", "Y"};
    c3_lcd_rect(71,122,178,37,0x0000u);
    for (unsigned i=0; i<5u; ++i) {
        uint16_t x = (uint16_t)(77u + 35u*i);
        c3_lcd_rect(x,127,27,16,color[i]);
        c3_lcd_text((int)x+11,147,name[i],0xFFFFu,1);
    }
}
#endif

#if C3_LCD_COLOR_DIAG
static void c3_color_diag_overlay(uint8_t profile, uint8_t swap_rb)
{
    char line1[] = "LCD BGR1 INV0";
    line1[7] = (profile & 2u) ? '1' : '0';
    line1[12] = (profile & 1u) ? '1' : '0';
    c3_lcd_rect(64,165,205,37,0x0000u);
    c3_lcd_text(72,169,line1,0xFFFFu,1);
    c3_lcd_text(72,181,swap_rb ? "CAM RB:SWAPPED" : "CAM RB:NORMAL ",0xFFFFu,1);
    c3_lcd_rect(57,207,225,12,0x0000u);
    c3_lcd_text(61,209,"4X:LCD NEXT ISO:CAM RB",0xFFFFu,1);
}
#endif

/* The save transaction has already been synced, closed and renamed before
 * this path; no success banner or UI-only delay is needed. Keep an explicit
 * 0 fallback to make a byte-identical v0.1.67 rollback build possible. */
static void show_save_success(void)
{
#if !C3_NO_SAVED
    c3_ui_message("SAVED");
    HAL_Delay(C3_SAVED_HOLD_MS);
#endif
}

#if C3_POSTSHOT
#if C3_POSTSHOT_KEEP_BG
/* The previous RGB preview is still in LCD GRAM. After a successful SD
 * retry, erase only the old recovery panel (not the entire preview). */
static void postshot_prepare_retry_background(const c3_settings_t *settings)
{
    if(c3_settings_wind(settings)&&!c3_settings_review(settings))
        c3_lcd_rect(12,76,296,113,0x0000u);
}
#endif
static void postshot_wind_hint(void)
{
    const char *key="WIND FILM LEVER";
    int width=24+c3_i18n_width(c3_i18n_translate(key),1);
    c3_lcd_rect(0,216,320,24,0x0000u);
    c3_ui_hint((320-width)/2,220,C3_HINT_FILM,key,0xFFFFu);
}
/* Called only after successful final rename and release of the held still.
 * No capture, menu navigation, sleep, or settings edits in this modal.
 * Return true if an input-consuming post-shot page was used. */
static uint8_t postshot_wait(const c3_settings_t *settings,c3_output_format_t format,uint8_t *sd_ok)
{
    const uint8_t review=c3_settings_review(settings),wind=c3_settings_wind(settings);
    if(!review&&!wind)return 0u;
    /* Arm at the committed-save boundary, before reading the file. Old
     * capture/save presses are discarded; a fresh winding stroke during
     * the decode is retained by the ISR and consumed after drawing. */
    c3_input_reset(read_buttons(),HAL_GetTick());
    if(review){
        /* c3_storage_save advances next_photo only after the final rename. */
        *sd_ok=c3_playback_show_saved(c3_storage_next_number()-1u,format);
    }else{
#if !C3_POSTSHOT_KEEP_BG
        c3_lcd_fill(0x0841u);
#endif
        /* Preserve the last preview in LCD GRAM. No SD re-read, FIFO
         * capture, extra framebuffer, or sensor mode change is needed. */
        c3_ui_message("SAVED");
    }
    c3_postshot_t state=c3_postshot_begin(review,wind,HAL_GetTick());
    if(state.state==C3_POST_WIND)postshot_wind_hint();
    /* A held lever is not a new winding action. Timing starts only after
     * the full review image or saved/error screen is drawn. */
    while(state.state!=C3_POST_DONE){
        c3_input_batch_t batch=c3_input_take();
        uint8_t before=state.state;
        c3_postshot_poll(&state,batch.events.pressed,HAL_GetTick());
        if(before!=C3_POST_WIND&&state.state==C3_POST_WIND)postshot_wind_hint();
        c3_sd_led_poll(HAL_GetTick());
        if(state.state!=C3_POST_DONE)HAL_Delay(10u);
    }
    c3_input_reset(read_buttons(),HAL_GetTick());
    return 1u;
}
static void postshot_resume_input(c3_ui_nav_t *nav,uint16_t *suppress,
                                  uint32_t *activity,uint8_t *dial)
{
    /* Consume all held buttons and events from the modal AND sensor restore.
     * Inhibit the lever's long-hold menu until it is released. */
    *activity=HAL_GetTick();*dial=read_shutter_dial();
    *suppress=read_buttons();nav->inhibit_film=!!(*suppress&C3_BTN_FILM);
    c3_input_reset(*suppress,*activity);
}
#endif

static const char *save_error_text(c3_save_result_t s)
{
    switch(s) {
        case C3_SAVE_NO_SD: return "NO SD";
        case C3_SAVE_NO_SPACE: return "NO PHOTO SLOT";
        case C3_SAVE_OPEN_ERROR: return "OPEN FAIL";
        case C3_SAVE_WRITE_ERROR: return "WRITE FAIL";
        case C3_SAVE_SYNC_ERROR: return "SYNC FAIL";
        case C3_SAVE_CLOSE_ERROR: return "CLOSE FAIL";
        case C3_SAVE_RENAME_ERROR: return "RENAME FAIL";
        case C3_SAVE_FIFO_ERROR: return "FIFO ERROR";
        default: return "SAVE FAILED";
    }
}

/* Keep LCD GRAM showing the previous view underneath this message. Because
 * the camera is now in RAW mode, it cannot draw a new RGB live preview until
 * the user commits or explicitly discards the pending image. */
static void show_pending_save_error(c3_save_result_t error)
{
    c3_lcd_rect(12, 76, 296, 113, 0x0000u);
    c3_ui_text(22, 87, "SAVE FAILED", 0xF800u, 2);
    c3_ui_text(22, 112, save_error_text(error), 0xFFE0u, 1);
    if (error == C3_SAVE_RENAME_ERROR)
        c3_ui_text(22,
#if C3_I18N
                    c3_i18n_is_chinese() ? 129 :
#endif
                    126, "CHECK SD TMP FILE", 0xFFE0u, 1);
#if C3_UI_ICONS
    c3_ui_hint(22,144,C3_HINT_SHOT,"RETRY",0xFFFFu);
    c3_ui_hint(22,162,C3_HINT_FILM,"HOLD - DISCARD",0xFFFFu);
#else
    c3_ui_text(22, 148, "SHOT: RETRY", 0xFFFFu, 1);
    c3_ui_text(22, 164, "HOLD FILM: DISCARD", 0xFFFFu, 1);
#endif
}

#if C3_ZOOM_AE_LOCK
static uint8_t preview_zoom4x;
static c3_zoom_hold_t zoom_hold;
#if C3_ZOOM_LOCK_DIAG
/* Display the actual failing stage, not fresh SCCB reads after reset. */
static void zoom_diag_failure(uint8_t stage)
{
    const char *reason="UNKNOWN";
    switch(stage){
      case 1u: reason="INIT: SENSOR";break;
      case 2u: reason="INIT: WINDOW";break;
      case 3u: reason="APPLY: LOCK VERIFY";break;
      case 4u: reason="INIT: VSYNC";break;
      case 11u: reason="SNAP: COM8 FREEZE";break;
      case 12u: reason="SNAP: AEC/GAIN 1";break;
      case 13u: reason="SNAP: WB READ";break;
      case 14u: reason="SNAP: AEC/GAIN 2";break;
      case 15u: reason="SNAP: AEC/GAIN CHANGE";break;
      case 16u: reason="SNAP: WB RANGE";break;
      case 17u: reason="SNAP: WB VERIFY";break;
      case 18u: reason="SNAP: ROW CONVERSION";break;
      case 19u: reason="SNAP: VSYNC WAIT";break;
    }
    c3_lcd_rect(22,136,276,24,0x0000u);
    c3_lcd_text(30,144,reason,0xFFE0u,1);
    HAL_Delay(2500u); /* diagnostic only; original 500ms otherwise */
}
#endif
static void zoom_release(void)
{
    zoom_hold.source.valid=0u;
    preview_zoom4x=0u;
    c3_exposure_set_preview_zoom(0u);
    c3_camera_preview_select_zoom(0u);
}
static void zoom_meter_seed(c3_meter_t *meter)
{
    if(!zoom_hold.source.valid)return;
    meter->exposure_rows=c3_zoom_hold_rows(&zoom_hold,C3_EXPOSURE_PREVIEW);
    meter->raw_gain=zoom_hold.source.gain_register;
    meter->gain_x16=c3_meter_decode_gain_x16(meter->raw_gain);
    meter->valid=meter->attempted=1u;
    meter->last_attempt_ms=HAL_GetTick();
    meter->warning=zoom_hold.warning;
}
static uint8_t zoom_apply_preview(const c3_exposure_t *s)
{
    if(zoom_hold.source.valid)
        return c3_zoom_hold_matches(&zoom_hold,s) &&
            c3_zoom_hold_apply(&zoom_hold,C3_EXPOSURE_PREVIEW,ov7725_read_reg_checked,ov7725_write_reg);
    return c3_exposure_prepare_timing(s,C3_EXPOSURE_PREVIEW,ov7725_read_reg_checked,ov7725_write_reg) &&
           c3_exposure_apply(s,C3_EXPOSURE_PREVIEW,ov7725_write_reg) &&
           c3_exposure_verify_manual_night(s,ov7725_read_reg_checked);
}
#endif

static void resume_preview(uint8_t *camera_ok, c3_fps_t *fps,
                           const c3_exposure_t *exposure, c3_meter_t *meter)
{
    *camera_ok = c3_camera_preview_init();
#if C3_ZOOM_AE_LOCK
    if(*camera_ok){
        c3_camera_preview_stop();
        if(!zoom_apply_preview(exposure)){
            *camera_ok=0u;c3_camera_last_init_stage=3u;
        }
    }
    /* Do not remain in a zoom crop with unknown/automatic exposure. */
    if(!*camera_ok && preview_zoom4x){
        zoom_release();
        c3_ui_message("4X LOCK FAIL");
#if C3_ZOOM_LOCK_DIAG
        zoom_diag_failure(c3_camera_last_init_stage);
#else
        HAL_Delay(500u);
#endif
        *camera_ok=c3_camera_preview_init();
        if(*camera_ok){c3_camera_preview_stop();if(!zoom_apply_preview(exposure)){
            *camera_ok=0u;c3_camera_last_init_stage=3u;
        }}
    }
#else
    if (*camera_ok && !(c3_exposure_prepare_timing(exposure,C3_EXPOSURE_PREVIEW,
                                               ov7725_read_reg_checked,ov7725_write_reg) &&
                        c3_exposure_apply(exposure, C3_EXPOSURE_PREVIEW,
                                          ov7725_write_reg))) {
        *camera_ok = 0u;
        c3_camera_last_init_stage = 3u;
    }
#endif
    if (*camera_ok) c3_camera_preview_rearm();
    c3_fps_reset(fps);
    c3_meter_reset(meter);
#if C3_ZOOM_AE_LOCK
    if(*camera_ok)zoom_meter_seed(meter);
#endif
    c3_focus_reset(&c3_focus_state);
    c3_score_reset(&c3_score_state);
    if (!*camera_ok) show_camera_error();
}

/* Fast exposure-only preview update. Sensor is ALREADY configured for
 * RGB565 and the currently selected zoom window. Never reset COM7, the PLL,
 * or AWB when the user changes shutter/ISO. Stop the FIFO producer before
 * SCCB writes, flush all old/partial frames, and resume at a VSYNC boundary.
 * Only the capture driver is rearmed; RAW and sleep still require full init. */
static void resume_preview_controls(uint8_t *camera_ok, c3_fps_t *fps,
                                    const c3_exposure_t *exposure, c3_meter_t *meter)
{
#if C3_ZOOM_AE_LOCK
    if(preview_zoom4x){
        if(!c3_zoom_hold_matches(&zoom_hold,exposure)){
            zoom_release();resume_preview(camera_ok,fps,exposure,meter);return;
        }
        if(*camera_ok){
            c3_camera_preview_rearm();c3_fps_reset(fps);c3_meter_reset(meter);
            zoom_meter_seed(meter);c3_focus_reset(&c3_focus_state);c3_score_reset(&c3_score_state);return;
        }
        resume_preview(camera_ok,fps,exposure,meter);return;
    }
#endif
#if C3_FAST_PREVIEW_CONTROL
    if (*camera_ok) {
        c3_camera_preview_stop();
        if (
#if C3_ZOOM_AE_LOCK
            zoom_apply_preview(exposure)
#else
            c3_exposure_prepare_timing(exposure,C3_EXPOSURE_PREVIEW,
                                          ov7725_read_reg_checked,ov7725_write_reg) &&
            c3_exposure_apply(exposure, C3_EXPOSURE_PREVIEW, ov7725_write_reg)
#endif
            ) {
            c3_camera_preview_rearm();
            c3_fps_reset(fps);
            c3_meter_reset(meter);
            c3_focus_reset(&c3_focus_state);
            c3_score_reset(&c3_score_state);
            return;
        }
    }
#endif
    /* Invalid settings / stale sensor: use the known working recovery. */
    resume_preview(camera_ok,fps,exposure,meter);
}

#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE && !C3_PREVIEW_PROBE
/* Do not reset the sensor after RAW. Keep its AWB/AEC state across the
 * RAW8->RGB565 transition, writing saved gains while COM8 auto is OFF.
 * If strict readback fails, recover using the existing full-init path.
 * The latter may require AWB reconvergence, but never risks a bad preview. */
volatile uint32_t c3_rgb_fast_restores;
volatile uint32_t c3_rgb_fast_restore_fallbacks;
static void resume_preview_after_raw(uint8_t *camera_ok,c3_fps_t *fps,
                                     const c3_exposure_t *exposure,c3_meter_t *meter,
                                     const c3_exposure_snapshot_t *held,
                                     uint8_t raw_was_fast)
{
    if (raw_was_fast && c3_camera_rgb_fast_restore()) {
#if C3_ZOOM_AE_LOCK
        if(zoom_hold.source.valid){
            if(zoom_apply_preview(exposure)){
                *camera_ok=1u;c3_camera_preview_rearm();c3_fps_reset(fps);c3_meter_reset(meter);
                zoom_meter_seed(meter);c3_focus_reset(&c3_focus_state);c3_score_reset(&c3_score_state);
                ++c3_rgb_fast_restores;return;
            }
            ++c3_rgb_fast_restore_fallbacks;
            resume_preview(camera_ok,fps,exposure,meter);return;
        }
#endif
        uint8_t ok=c3_exposure_restore_preview_snapshot(
            held,exposure,ov7725_write_reg);
        uint8_t r,g,b;
        if (ok && c3_dng_get_preview_awb_gains(&r,&g,&b)) {
            /* These are actual ISP gains, NOT tint-inverted DNG neutrals. */
            ok=(uint8_t)(!ov7725_write_reg(0x02u,r) &&
                         !ov7725_write_reg(0x03u,g) &&
                         !ov7725_write_reg(0x01u,b));
        }
        if (ok &&
#if C3_ZOOM_AE_LOCK
            zoom_apply_preview(exposure)
#else
            c3_exposure_prepare_timing(exposure,C3_EXPOSURE_PREVIEW,
                                          ov7725_read_reg_checked,ov7725_write_reg) &&
            c3_exposure_apply(exposure,C3_EXPOSURE_PREVIEW,
                                     ov7725_write_reg)
#endif
            ) {
            *camera_ok=1u;
            c3_camera_preview_rearm();
            c3_fps_reset(fps);
            c3_meter_reset(meter);
            c3_focus_reset(&c3_focus_state);
            c3_score_reset(&c3_score_state);
            ++c3_rgb_fast_restores;
            return;
        }
    }
    if (raw_was_fast) ++c3_rgb_fast_restore_fallbacks;
    resume_preview(camera_ok,fps,exposure,meter);
}
#endif

#if !C3_PREVIEW_PROBE
/* All FIFO/DMA/SD operations are already quiescent when this is called.
 * SysTick remains at 1 kHz to debounce wake keys. Do not enter STOP mode:
 * that would lose the 72 MHz PLL and the active timer/SD/FSMC setup. */
static void c3_run_sleep(c3_ui_nav_t *nav,uint8_t *camera_ok,c3_fps_t *fps,
                         c3_exposure_t *exposure,c3_meter_t *meter)
{
    uint8_t dial_at_sleep=read_shutter_dial();
    c3_camera_preview_stop();
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn); /* sensor VSYNC cannot wake the CPU */
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    /* COM2 bit4 is documented OV7725 SCCB soft sleep; allow full RGB
     * sensor init to restore the previous zoom/exposure configuration. */
    (void)ov7725_write_reg(0x09u,0x11u);
    c3_lcd_enter_sleep();
    c3_sleep_led_start();
    uint32_t sleep_start=HAL_GetTick();
    uint32_t last_led_update=sleep_start-25u;
    for (;;) {
        uint32_t tick=HAL_GetTick();
        if (read_buttons() || read_shutter_dial()!=dial_at_sleep) break;
        if ((uint32_t)(tick-last_led_update)>=25u) {
            c3_sleep_led_update(tick-sleep_start);
            last_led_update=tick;
        }
        /* Interrupts (SysTick + TIM2) wake WFI, but only physical keys / dial
         * end the sleep session. A waking shutter press is consumed below. */
        __WFI();
    }
    c3_sleep_led_stop();
    (void)ov7725_write_reg(0x09u,0x01u);
    HAL_Delay(10u);
    c3_lcd_leave_sleep();
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    /* Exiting to the live view also avoids resurrecting a stale menu screen. */
    c3_ui_nav_reset(nav,read_shutter_dial(),HAL_GetTick());
    c3_exposure_sync_dial(exposure,read_shutter_dial(),HAL_GetTick());
#if C3_ZOOM_AE_LOCK
    zoom_release(); /* wake starts a fresh 1X metering session */
#endif
    resume_preview(camera_ok,fps,exposure,meter);
    c3_input_reset(read_buttons(),HAL_GetTick());
}

static void c3_note_activity(uint32_t *last_activity,const c3_input_batch_t *b,
                             uint8_t dial,uint8_t *last_dial,uint16_t pressed)
{
    if (dial!=*last_dial || pressed || b->events.pressed ||
        b->events.released || b->events.short_released ||
        b->events.long_pressed || b->events.long_released) {
        *last_activity=HAL_GetTick();
    }
    *last_dial=dial;
}
#endif

#ifndef C3_OVERLAY_DIAG
#define C3_OVERLAY_DIAG 0
#endif
#if C3_OVERLAY_DIAG && C3_LCD_COLOR_DIAG
#error "Overlay and LCD color test controls cannot run together"
#endif
#if C3_OVERLAY_DIAG && !C3_PREVIEW_PROBE
#error "Overlay diagnostic is strictly read-only"
#endif

#if C3_PREVIEW_PROBE
/* Real preview, but deliberately no still capture / filesystem / flash
 * writes. We want the first camera frame, not a corrupted test photograph. */
static void c3_preview_probe_loop(uint8_t camera_ok,
                                  const c3_settings_t *stored_settings,
                                  c3_meter_t *meter)
{
    if (!camera_ok) {
        c3_boot_stage = 0xE420u;
        show_camera_error();
        for (;;) HAL_Delay(200u);
    }

    c3_settings_t view = *stored_settings;
    view.peaking = 0u;
    view.monochrome = 0u;
#if C3_PROFILE_HUD
    view.grid = 0u; /* Force known simple preview path for profiling. */
#endif
    view.osd = 1u;
    view.fps = 1u;
    c3_fps_t fps;
    c3_fps_reset(&fps);
    c3_boot_stage = 0x20u;
    c3_ui_message("PREVIEW TEST");
    if (c3_ov_verify_warn_mask != 0u) {
        if (c3_ov_warn_first_reg == 0xFFu) {
            c3_lcd_text(12,152,"POST PID UNSTABLE",0xFFE0u,1);
            c3_lcd_text(12,168,"CHECK SCCB DATA",0xFFE0u,1);
        } else {
            char line[] = "WARN R00 E00 G00";
            line[6] = c3_hex_digit((uint8_t)(c3_ov_warn_first_reg >> 4));
            line[7] = c3_hex_digit((uint8_t)c3_ov_warn_first_reg);
            line[10] = c3_hex_digit((uint8_t)(c3_ov_warn_first_expected >> 4));
            line[11] = c3_hex_digit((uint8_t)c3_ov_warn_first_expected);
            line[14] = c3_hex_digit((uint8_t)(c3_ov_warn_first_actual >> 4));
            line[15] = c3_hex_digit((uint8_t)c3_ov_warn_first_actual);
            c3_lcd_text(12,152,line,0xFFE0u,1);
            c3_lcd_text(12,168,"READBACK UNVERIFIED",0xFFE0u,1);
        }
    } else {
        c3_lcd_text(12,152,"READBACK OK",0x07E0u,1);
    }
    c3_lcd_text(12,188,"NO SD / NO CAPTURE",0xFFFFu,1);
#if C3_OVERLAY_DIAG
    c3_lcd_text(12,204,"4X:GRID  ISO:PEAK",0xFFFFu,1);
#endif
    HAL_Delay(1400u);
#if C3_OVERLAY_DIAG
    /* ISR-sampled keys; do not touch the 4X sensor zoom or capture path. */
    c3_input_reset(read_buttons(), HAL_GetTick());
#endif
#if C3_LCD_COLOR_DIAG
    /* This probe never mounts the SD card, saves settings, or captures a
     * still. No GPIO output pins are changed by the button handling. */
    uint8_t lcd_profile = (uint8_t)((C3_LCD_DEFAULT_BGR ? 2u : 0u) |
                                    (C3_LCD_DEFAULT_INV_ON ? 1u : 0u));
    uint8_t cam_com3 = (uint8_t)C3_CAM_COM3_RGB;
    c3_input_reset(read_buttons(), HAL_GetTick());
#endif
    c3_camera_preview_rearm();

    for (;;) {
        if (!c3_camera_preview_acquire(1300u)) {
            c3_boot_stage = 0xE620u; /* Sync OK, FIFO scheduling failed */
            c3_ui_message("FIFO PREVIEW TIMEOUT");
            for (;;) HAL_Delay(250u);
        }
        c3_camera_preview_draw(&view,0u,0u,0u,fps.tenths,
                               C3_SHOOT_P,0u,0u,0u,0,0u,meter);
        c3_camera_preview_release();
#if C3_COLOR_SWATCH_DIAG
        c3_show_lcd_color_reference();
#endif
        (void)c3_fps_frame(&fps, HAL_GetTick());
#if C3_OVERLAY_DIAG
        c3_input_batch_t overlay_keys = c3_input_take();
        if (overlay_keys.events.short_released & C3_BTN_4X) {
            view.grid ^= 1u;
            c3_fps_reset(&fps);
        }
        if (overlay_keys.events.short_released & C3_BTN_ISO) {
            view.peaking ^= 1u;
            c3_focus_reset(&c3_focus_state);
            c3_score_reset(&c3_score_state);
            c3_fps_reset(&fps);
        }
#endif
#if C3_LCD_COLOR_DIAG
        c3_color_diag_overlay(lcd_profile, (uint8_t)((cam_com3 & 0x20u)!=0u));
        c3_input_batch_t keys = c3_input_take();
        if (keys.events.short_released & C3_BTN_4X) {
            /* 4 combinations of MADCTL BGR (0/1) and inversion (0/1).
             * Changes only display interpretation, not pixel bytes. */
            lcd_profile = (uint8_t)((lcd_profile + 1u) & 3u);
            c3_lcd_set_color_mode((uint8_t)((lcd_profile & 2u)!=0u),
                                  (uint8_t)((lcd_profile & 1u)!=0u));
        }
        if (keys.events.short_released & C3_BTN_ISO) {
            /* Stop producer before SCCB write; never modify a frame while
             * the FIFO is being consumed. Colour bit only: direction stays. */
            c3_camera_preview_stop();
            cam_com3 ^= 0x20u;
            (void)ov7725_write_reg(0x0Cu /* COM3 */, (uint8_t)(cam_com3 |
                         (C3_CAM_COLOR_BAR ? 0x01u : 0x00u)));
            HAL_Delay(20u);
            c3_camera_preview_rearm();
            c3_fps_reset(&fps);
        }
#endif
        if (c3_ov_verify_warn_mask != 0u) {
            /* Draw warning after streaming, never alter FIFO pixels. */
            c3_lcd_rect(73,102,174,15,0x0000u);
            c3_lcd_text(77,105,"SCCB VERIFY WARN",0xFFE0u,1);
        }
    }
}
#endif

int main(void)
{
    HAL_Init();
#if C3_BOOT_DIAG
    boot_origin_ms=HAL_GetTick();
#endif
    SystemClock_Config();
    delay_init(72);
    MX_GPIO_Init();
    /* Explicitly hold LCD in reset with its backlight OFF while power
     * settles. gpio.c preloads these LOW *before* enabling output mode. */
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN | C3_LCD_RESET_PIN, GPIO_PIN_RESET);
    c3_boot_stage = 1u;
#if C3_BOOT_OVERLAP
    /* MX_GPIO_Init has released SCL/SDA high in open-drain mode.
     * LCD RESET remains low; FIFO WEN remains off. */
    const uint32_t cam_power_tick=HAL_GetTick();
#endif
    /* This is a read-only operation; defaults to 180 degrees on erased Flash. */
    c3_settings_t settings;
    (void)c3_settings_flash_load(&settings);
#if C3_I18N
    c3_i18n_set_language(c3_settings_language(&settings));
#endif
#if C3_PREVIEW_PROFILE
    c3_camera_profile_init();
#endif
    MX_FSMC_Init();
    /* v0.5 LCD probe succeeded with a delayed FIRST initialization.
     * Crucially, this wait is BEFORE /RESET is released or any LCD
     * commands are sent. It is not a periodic display re-init. */
    c3_boot_stage = 0x11u; /* LCD held in reset, waiting for power */
    HAL_Delay(C3_LCD_POWER_SETTLE_MS);
    c3_lcd_init();
    c3_lcd_set_rotation(settings.rotate_180);
    c3_boot_stage = 2u;
    c3_ui_message("C3M1 BOOT");
    /* Backlight stays OFF through GRAM clear, orientation and first message. */
    c3_lcd_backlight_on();
#if C3_LANGUAGE_SETUP && !C3_PREVIEW_PROBE && !C3_FIFO_DMA_PROBE && !C3_SCCB_SCAN_DIAG
    if(c3_settings_language_pending(&settings)) {
        (void)choose_language(&settings,1u);
        c3_lcd_fill(0u);
        c3_ui_message("C3M1 BOOT");
    }
#endif
#if C3_BOOT_DIAG
    boot_lcd_ms=HAL_GetTick()-boot_origin_ms;
    const uint32_t boot_cam_start=HAL_GetTick();
#endif

    /* Initialize camera before the SD stack. Diagnostic v0.1 proved SCCB,
     * exposure and 0x0F VSYNC/s work without mounting a card; this order
     * differentiates camera failures from SD startup hangs. */
    c3_meter_t meter;
    c3_meter_reset(&meter);
    c3_exposure_t exposure;
    #if C3_FIFO_DMA_PROBE
    c3_exposure_init(&exposure, 7u /* 1/100 S, AUTO gain */, HAL_GetTick());
#else
    c3_exposure_init(&exposure, read_shutter_dial(), HAL_GetTick());
#endif
    c3_boot_stage = 3u;
    /* Reproduce the working diagnostic firmware's power-on timing and
     * initial reset-before-init sequence. This is a hardware hypothesis,
     * not proof that power-up timing caused the original failure. */
    c3_ui_message("CAM WARMUP");
#if C3_BOOT_OVERLAP
    const uint32_t cam_wait=c3_boot_remaining_ms(cam_power_tick,HAL_GetTick(),C3_CAM_POWER_SETTLE_MS);
    if(cam_wait)HAL_Delay(cam_wait);
#else
    HAL_Delay(C3_CAM_POWER_SETTLE_MS);
#endif
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(5u);
    c3_sccb_half_period_us = C3_SCCB_MAIN_HALF_US; /* v146: default 50us, optional 100us */
    sccb_init();
    (void)ov7725_write_reg(0x12u,0x80u);
    HAL_Delay(60u);
#if C3_SCCB_SCAN_DIAG
    /* Dedicated read-only SCCB matrix UI. No capture / SD / settings writes. */
    c3_sccb_diag_run();
    return 0;
#endif
#if C3_PREVIEW_PROBE && !C3_FIFO_PROBE
    c3_ui_message("SCCB SPEED TEST");
    c3_sccb_probe_three_speeds();
    c3_sccb_show_probe_counts();
    HAL_Delay(4000u); /* screenshot-friendly A/B/C display before init */
#endif
    c3_ui_message("CAM INIT");
    uint8_t camera_ok = c3_camera_preview_init();
    c3_cam_init_first_code = camera_ok ? 0u : c3_camera_last_sensor_code;
    if (!camera_ok && c3_camera_last_init_stage == 1u) {
        /* One bounded retry only; leave detailed failure data on LCD.
         * Never loop indefinitely or pretend the sensor is healthy. */
        HAL_Delay(150u);
        camera_ok = c3_camera_preview_init();
    }
#if C3_ZOOM_AE_LOCK
    if(camera_ok)c3_camera_preview_stop();
    if(camera_ok && !zoom_apply_preview(&exposure)){
#else
    if (camera_ok && !(c3_exposure_prepare_timing(&exposure,C3_EXPOSURE_PREVIEW,
                                               ov7725_read_reg_checked,ov7725_write_reg) &&
                   c3_exposure_apply(&exposure, C3_EXPOSURE_PREVIEW,
                                             ov7725_write_reg))) {
#endif
        camera_ok = 0u;
        c3_camera_last_init_stage = 3u;
    }
    if (camera_ok) c3_camera_preview_stop(); /* no capture while mounting SD */
    c3_boot_stage = 4u;
    if (camera_ok) c3_ui_message("CAM READY");
    else show_camera_error();
#if C3_BOOT_DIAG
    boot_cam_ms=HAL_GetTick()-boot_cam_start;
    boot_cam_ok=camera_ok;
#endif

#if C3_FIFO_DMA_PROBE
    c3_fifo_dma_probe_run(camera_ok); /* never enter SD/Flash write path */
#elif C3_PREVIEW_PROBE
    /* Dedicated read-only image-path test. Keep all external and internal
     * writes disabled until camera / FIFO pixels are actually validated. */
#if C3_FIFO_PROBE
    c3_fifo_probe_run(camera_ok);
#else
    c3_preview_probe_loop(camera_ok, &settings, &meter);
#endif
    return 0; /* unreachable */
#else
    c3_boot_stage = 5u;
#if C3_BOOT_DIAG
    const uint32_t boot_sd_start=HAL_GetTick();
#endif
    c3_ui_message("SD INIT");
    MX_SDIO_SD_Init();
    MX_FATFS_Init();
    uint8_t sd_ok = c3_storage_mount();
#if C3_BOOT_DIAG
    boot_sd_ms=HAL_GetTick()-boot_sd_start;
    boot_sd_ok=sd_ok;
#endif
    if (camera_ok) c3_camera_preview_rearm();
    uint8_t settings_dirty = 0u;
    uint32_t battery_refresh_tick = HAL_GetTick();
    /* Initial sample after the first normal preview frame. HAL_GetTick()
     * subtraction is safe even across the 32-bit millisecond wrap. */
    uint32_t osd_battery_tick = HAL_GetTick() - 5000u;
    uint32_t settings_change_tick = HAL_GetTick();
    uint32_t sleep_last_activity=HAL_GetTick();
    uint8_t sleep_last_dial=read_shutter_dial();
    uint16_t suppress_preview_keys = 0u;
#if !C3_ZOOM_AE_LOCK
    uint8_t preview_zoom4x = 0u; /* 1x/4x preview only, not persisted */
#endif
    c3_ui_nav_t nav;
#if C3_PLAYBACK_ENABLE
    c3_playback_t playback={0};
#endif
    c3_fps_t fps;
    c3_capture_recovery_t recovery;
    c3_output_format_t pending_format = C3_OUTPUT_DNG;
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
    /* Must survive the SD write error / retry / explicit-discard menu. */
    uint8_t raw_noreset_pending=0u;
    c3_exposure_snapshot_t raw_restore_snapshot={0};
#endif
    c3_recovery_reset(&recovery);
    c3_input_reset(read_buttons(), HAL_GetTick());
    c3_ui_nav_reset(&nav, read_shutter_dial(), HAL_GetTick());
    c3_fps_reset(&fps);
    c3_focus_reset(&c3_focus_state);
    c3_score_reset(&c3_score_state);
    if (!camera_ok) show_camera_error();
    c3_boot_stage = 6u;

    uint32_t retry_tick = HAL_GetTick();
#if C3_STARTUP_HINT
    /* First boot language selection has already completed above; the hint
     * starts only after the first successfully rendered preview frame. */
    c3_startup_hint_t startup_hint={0};
#endif
    while (1) {
        /* One cheap GPIO update per foreground iteration, never per pixel.
         * Sleeps are handled by TIM2 and SD_write itself lights the LED. */
        c3_sd_led_poll(HAL_GetTick());
#if C3_POSTSHOT
        uint8_t postshot_used=0u;
#endif
        if (c3_recovery_can_retry(&recovery)) {
            /* Never recapture or switch the sensor while the unsaved DNG/BMP
             * source frame remains in the AL422B FIFO. */
            c3_input_batch_t batch = c3_input_take();
            c3_button_events_t key = batch.events;
            if (key.pressed & C3_BTN_SHUTTER) {
                c3_ui_message("RETRYING");
                c3_save_result_t result = c3_storage_save(pending_format);
                c3_recovery_save_finished(&recovery, result);
                sd_ok = (uint8_t)(result == C3_SAVE_OK);
                if (result == C3_SAVE_OK) {
                    show_save_success();
                    c3_camera_raw_discard();
#if C3_POSTSHOT
#if C3_POSTSHOT_KEEP_BG
                    postshot_prepare_retry_background(&settings);
#endif
                    postshot_used=postshot_wait(&settings,pending_format,&sd_ok);
#endif
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
                    resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                             &raw_restore_snapshot,
                                             raw_noreset_pending);
                    raw_noreset_pending=0u;
#else
                    resume_preview(&camera_ok, &fps, &exposure, &meter);
#endif
                    c3_input_reset(read_buttons(), HAL_GetTick());
#if C3_POSTSHOT
                    if(postshot_used)postshot_resume_input(&nav,&suppress_preview_keys,&sleep_last_activity,&sleep_last_dial);
#endif
                } else {
                    show_pending_save_error(result);
                    /* Do not auto-retry because of a key pressed while the
                     * preceding SD operation was blocking the foreground. */
                    c3_input_reset(read_buttons(), HAL_GetTick());
                }
            } else if (key.long_pressed & C3_BTN_FILM) {
                /* Explicit loss of the image: user must hold FILM. */
                c3_camera_raw_discard();
                c3_recovery_discard(&recovery);
                c3_ui_message("DISCARDED");
                HAL_Delay(200u);
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
                resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                         &raw_restore_snapshot,raw_noreset_pending);
                raw_noreset_pending=0u;
#else
                resume_preview(&camera_ok, &fps, &exposure, &meter);
#endif
                c3_input_reset(read_buttons(), HAL_GetTick());
            }
            HAL_Delay(10u);  /* Buttons remain responsive without burning CPU */
            continue;
        }

        /* The lever opens an independent static screen. Its contents live in
         * LCD GRAM; no camera frame, RAM framebuffer or FIFO writes are needed
         * while MODE or MENU is visible. In MENU the dial is exclusively a
         * relative UI input, not an exposure input. */
        if (nav.page != C3_UI_PREVIEW) {
            uint32_t now = HAL_GetTick();
            c3_input_batch_t batch = c3_input_take();
            c3_note_activity(&sleep_last_activity,&batch,read_shutter_dial(),
                             &sleep_last_dial,read_buttons());
            now = HAL_GetTick(); /* never compare an older tick to fresh activity */
            /* Allow timeouts in MENU/MODE as well as live view. Persist any
             * edited preferences before sleep; failed saves forbid sleep. */
            if (camera_ok && c3_sleep_due(settings.sleep_timer,now,
                                 sleep_last_activity,read_buttons())) {
                uint8_t can_sleep=1u;
                if (settings_dirty) {
                    if (c3_settings_flash_save(&settings)) settings_dirty=0u;
                    else {
                        can_sleep=0u;
                        sleep_last_activity=now;
                        c3_ui_message("SET SAVE ERROR");
                    }
                }
                if (can_sleep) {
                    c3_run_sleep(&nav,&camera_ok,&fps,&exposure,&meter);
                    sleep_last_activity=HAL_GetTick();
                    sleep_last_dial=read_shutter_dial();
                    suppress_preview_keys=read_buttons();
                    continue;
                }
            }
            c3_button_events_t key = batch.events;
            uint16_t change = c3_ui_nav_poll(&nav,key,batch.stable,
                                            read_shutter_dial(),now,&settings);
#if C3_LANGUAGE_SETUP
            if (change & C3_NAV_LANGUAGE_OPEN) {
                if(choose_language(&settings,0u)) settings_dirty=0u;
                /* Dial movement within the child page is not a parent scroll. */
                nav.dial_stable=nav.dial_candidate=read_shutter_dial()&15u;
                nav.dial_changed_ms=HAL_GetTick();
                sleep_last_activity=HAL_GetTick();
                sleep_last_dial=read_shutter_dial();
                c3_menu_draw_settings(&nav,&settings);
                continue;
            }
#endif
#if C3_MENU_ACTIONS
            if (change & C3_NAV_CONFIRM_OPEN) {
                c3_menu_draw_confirm(nav.selected);
                continue;
            }
            if (change & C3_NAV_CONFIRM_CANCEL) {
                c3_menu_draw_settings(&nav,&settings);
                continue;
            }
            if (change & C3_NAV_RESET) {
                c3_settings_t defaults;
                c3_settings_defaults(&defaults);
                if (c3_settings_flash_save(&defaults)) {
                    c3_ui_message("SETTINGS RESET"); HAL_Delay(650u);
                    settings=defaults; settings_dirty=0u;
                    c3_lcd_set_rotation(settings.rotate_180);
#if C3_I18N
                    c3_i18n_set_language(c3_settings_language(&settings));
#endif
#if C3_ZOOM_AE_LOCK
                    zoom_release();
#endif
#if C3_LANGUAGE_SETUP
                    /* Defaults, including pending language, are already
                     * committed. Power loss here requires the picker next boot. */
#if C3_DEFAULTS_V218 && C3_STARTUP_HINT
                    if (choose_language(&settings,1u))
                        c3_startup_hint_arm(&startup_hint);
#else
                    (void)choose_language(&settings,1u);
#endif
#endif
                    c3_exposure_init(&exposure,read_shutter_dial(),HAL_GetTick());
                    c3_ui_nav_reset(&nav,read_shutter_dial(),HAL_GetTick());
                    resume_preview(&camera_ok,&fps,&exposure,&meter);
                } else {
                    c3_ui_message("SET SAVE ERROR"); HAL_Delay(900u);
                    c3_menu_draw_settings(&nav,&settings);
                }
                sleep_last_activity=HAL_GetTick();
                suppress_preview_keys=read_buttons();
                c3_input_reset(suppress_preview_keys,sleep_last_activity);
                continue;
            }
            if (change & C3_NAV_FORMAT) {
                c3_camera_preview_stop();
                c3_menu_draw_format_status("FORMATTING",1u);
                c3_format_result_t result=c3_storage_format_card();
                sd_ok=(uint8_t)(result==C3_FORMAT_OK);
                if (result==C3_FORMAT_TOO_LARGE) sd_ok=c3_storage_mount();
                c3_menu_draw_format_status(result==C3_FORMAT_OK?"FORMAT COMPLETE":
                    result==C3_FORMAT_TOO_LARGE?"CARD TOO LARGE":
                    result==C3_FORMAT_NOT_READY?"CARD NOT READY":
                    result==C3_FORMAT_MOUNT_ERROR?"FORMAT MOUNT ERROR":"FORMAT FAILED",0u);
                HAL_Delay(1200u);
                c3_menu_draw_settings(&nav,&settings);
                sleep_last_activity=HAL_GetTick();
                c3_input_reset(read_buttons(),sleep_last_activity);
                continue;
            }
#endif
#if C3_PLAYBACK_ENABLE
            /* Entry is from MODE only, where preview DMA and FIFO writes
             * have already stopped. The pending-still retry gate above
             * always takes precedence. A completed load leaves no file open. */
            if (change & C3_NAV_PLAY_OPEN) {
                c3_camera_preview_stop();
                sd_ok=c3_playback_enter(&playback);
                sleep_last_activity=HAL_GetTick();
                continue;
            }
            if (nav.page==C3_UI_PLAY) {
                if (change & C3_NAV_PLAY_STEP) {
                    sd_ok=c3_playback_step(&playback,nav.ev_step_delta);
                    sleep_last_activity=HAL_GetTick();
                } else if (key.pressed & C3_BTN_ISO) {
                    sd_ok=c3_playback_toggle_info(&playback);
                    sleep_last_activity=HAL_GetTick();
                }
                HAL_Delay(10u);
                continue;
            }
#endif
#if C3_FUNCTION_MENU
            if (change & C3_NAV_ABOUT_OPEN) {
                c3_menu_draw_about();
                continue;
            }
            if (change & C3_NAV_ABOUT_BACK) {
                c3_menu_draw_settings(&nav,&settings);
                continue;
            }
#endif
            if (change & C3_NAV_EV_ADJUST)
                (void)c3_exposure_ev_adjust(&exposure,nav.ev_step_delta);
            if (change & C3_NAV_EV_ADJUST) {
                c3_menu_draw_mode(&exposure);
#if C3_FUNCTION_MENU
                battery_refresh_tick=HAL_GetTick();
#endif
            }
            if (change & C3_NAV_MENU_OPEN)
                c3_menu_draw_settings(&nav,&settings);
            if (change & C3_NAV_SETTING) {
                /* MADCTL rotates the LCD GRAM address walk in hardware.
                 * Do not rotate source image or touch the FIFO stream. */
                if (nav.selected == C3_SETTING_ROTATION)
                    c3_lcd_set_rotation(settings.rotate_180);
                settings_dirty = 1u;
                settings_change_tick = now;
                c3_focus_reset(&c3_focus_state);
                c3_score_reset(&c3_score_state);
                c3_fps_reset(&fps);
            }
            if (change & (C3_NAV_CURSOR | C3_NAV_SETTING)) {
                c3_menu_draw_settings(&nav,&settings);
                battery_refresh_tick = now;
            }
#if C3_FUNCTION_MENU
            if (nav.page == C3_UI_MODE &&
                (uint32_t)(HAL_GetTick()-battery_refresh_tick)>=2000u) {
                c3_menu_draw_battery();
                battery_refresh_tick=HAL_GetTick();
            }
#else
            else if (nav.page == C3_UI_MENU &&
                       nav.selected == C3_SETTING_BATTERY &&
                       (uint32_t)(now - battery_refresh_tick) >= 2000u) {
                /* Never sample ADC or redraw the menu during live preview. */
                c3_menu_draw_settings(&nav,&settings);
                battery_refresh_tick = now;
            }
#endif
            if (change & C3_NAV_BACK) {
                /* The physical dial may have moved during navigation. Apply
                 * its current stable position only AFTER returning to preview. */
                c3_exposure_sync_dial(&exposure,read_shutter_dial(),now);
                if (settings_dirty) {
                    if (c3_settings_flash_save(&settings)) settings_dirty=0u;
                    else {
                        c3_ui_message("SET SAVE ERROR");
                        HAL_Delay(250u);
                        settings_change_tick = HAL_GetTick();
                    }
                }
                /* Menu only stopped the FIFO; the OV7725 kept running.
                 * Reapply changed P/S/M/EV values without a hard reset. */
                resume_preview_controls(&camera_ok,&fps,&exposure,&meter);
                suppress_preview_keys = read_buttons();
                c3_input_reset(suppress_preview_keys,HAL_GetTick());
            } else {
                HAL_Delay(10u);
            }
            continue;
        }

        if (!camera_ok) {
            if ((uint32_t)(HAL_GetTick() - retry_tick) > 1500u) {
                retry_tick = HAL_GetTick();
                resume_preview(&camera_ok, &fps, &exposure, &meter);
            }
            HAL_Delay(50u);
            continue;
        }

        /* Commit shortcut changes after an idle delay, only while FIFO
         * capture can be stopped safely. Avoid writing Flash every frame. */
        if (settings_dirty &&
            (uint32_t)(HAL_GetTick()-settings_change_tick) >= 10000u &&
            (read_buttons() & BUTTONS) == 0u) {
            c3_camera_preview_stop();
            if (c3_settings_flash_save(&settings)) settings_dirty = 0u;
            settings_change_tick = HAL_GetTick();
            c3_camera_preview_rearm();
        }

        if (c3_camera_preview_acquire(800u)) {
#if (C3_BOOT_DIAG || C3_STARTUP_HINT) && C3_FIFO_DMA_LIVE_ROWS
            uint32_t boot_errors_before=c3_live_errors;
#endif
            c3_camera_preview_draw(&settings, preview_zoom4x, sd_ok,
                                   c3_storage_next_number(),fps.tenths,
                                   (uint8_t)exposure.shoot_mode,
                                   c3_exposure_effective_dial(&exposure),
                                   c3_exposure_effective_gain(&exposure),
                                   exposure.dial_code, exposure.ev_half_steps,
                                   c3_exposure_ev_active(&exposure), &meter);
            c3_camera_preview_release();
#if C3_STARTUP_HINT
            if(c3_startup_hint_frame(&startup_hint,c3_settings_startup_hint(&settings),
#if C3_FIFO_DMA_LIVE_ROWS
                                     boot_errors_before==c3_live_errors,
#else
                                     1u,
#endif
                                     HAL_GetTick()))c3_ui_draw_startup_hint();
#endif
#if C3_BOOT_DIAG
            /* An aborted DMA render is not the first complete image. */
            if(!boot_first_seen
#if C3_FIFO_DMA_LIVE_ROWS
                && boot_errors_before==c3_live_errors
#endif
            ){
                boot_first_ms=HAL_GetTick()-boot_origin_ms;
                boot_first_seen=1u;
            }
            if(boot_first_seen && (uint32_t)(HAL_GetTick()-boot_origin_ms-boot_first_ms)<8000u)
                boot_show_timing();
#endif
#if C3_COLOR_SWATCH_DIAG
            /* Draw AFTER the FIFO has been released; LCD pixels only. */
            c3_show_lcd_color_reference();
#endif
            /* Mainline experimental warning persists on preview frames.
             * Do not mistake a valid VSYNC/frame for proven SCCB writes. */
            if (c3_ov_verify_warn_mask != 0u) {
                c3_lcd_rect(73,102,174,15,0x0000u);
                c3_lcd_text(77,105,"SCCB VERIFY WARN",0xFFE0u,1);
            }
            /* SCCB is intentionally outside FIFO->LCD pixel streaming.
             * Never poll while a RAW frame is being retained for retry.
             * In P/S, show actual PREVIEW AEC/AGC register readback. */
            if (exposure.shoot_mode != C3_SHOOT_M &&
                c3_meter_poll(&meter, HAL_GetTick(), ov7725_read_reg_checked)) {
#if C3_ZOOM_AE_LOCK
                if(zoom_hold.source.valid)meter.warning=zoom_hold.warning;
                else
#endif
                {
                    uint8_t aew,aeb;
                    c3_exposure_ev_targets(exposure.ev_half_steps,&aew,&aeb,0);
                    c3_meter_update_warning(&meter,(uint8_t)exposure.shoot_mode,
                        aeb,aew,ov7725_read_reg_checked);
                }
            }
#if C3_METER_DIAG
            if (exposure.shoot_mode != C3_SHOOT_M) c3_show_meter_diag(&meter);
            c3_show_night_diag((uint8_t)exposure.shoot_mode,HAL_GetTick()
#if C3_ZOOM_AE_LOCK
                               ,preview_zoom4x
#endif
                               );
#endif
            if (settings.fps) (void)c3_fps_frame(&fps, HAL_GetTick());
        } else {
            c3_ui_message("CAM TIMEOUT");
            camera_ok = 0u;
            retry_tick = HAL_GetTick();
            continue;
        }
        uint32_t now = HAL_GetTick();
        /* At this point FIFO output is released and LCD DMA is finished.
         * Read ADC only every 5 s in normal live preview, never inside
         * camera_preview_draw(), while a menu is open, or during RAW hold. */
        if ((uint32_t)(now - osd_battery_tick) >= 5000u) {
            uint16_t mv = 0u;
            const uint8_t battery_ok = c3_battery_read_mv(&mv);
            c3_osd_set_battery(battery_ok ? c3_battery_estimate_percent(mv) : 0u,
                               battery_ok);
            osd_battery_tick = HAL_GetTick();
        }
        c3_input_batch_t batch = c3_input_take();
        c3_note_activity(&sleep_last_activity,&batch,read_shutter_dial(),
                         &sleep_last_dial,read_buttons());
        if (camera_ok && c3_sleep_due(settings.sleep_timer,HAL_GetTick(),
                                      sleep_last_activity,read_buttons())) {
            uint8_t can_sleep=1u;
            if (settings_dirty) {
                c3_camera_preview_stop();
                if (c3_settings_flash_save(&settings)) settings_dirty=0u;
                else {
                    can_sleep=0u;
                    sleep_last_activity=HAL_GetTick();
                    c3_camera_preview_rearm();
                }
            }
            if (can_sleep) {
                c3_run_sleep(&nav,&camera_ok,&fps,&exposure,&meter);
                sleep_last_activity=HAL_GetTick();
                sleep_last_dial=read_shutter_dial();
                suppress_preview_keys=read_buttons();
                continue;
            }
        }
        c3_button_events_t key = batch.events;
        /* These presses may have occurred entirely during a long frame
         * render. FILM's chord suppression must retain that history. */
        suppress_preview_keys |= batch.film_chords;
        if (batch.stable & C3_BTN_FILM)
            suppress_preview_keys |= (uint16_t)(batch.stable & (C3_BTN_ISO | C3_BTN_4X));
        /* The MODE detector sees the complete captured event batch: a fast
         * 4X tap near FILM's 650ms threshold must not be discarded before
         * it can select SETTINGS. Filter the chords only for normal preview
         * shortcuts after handling the modal transition. */
        uint16_t action = c3_ui_nav_poll(&nav,key,batch.stable,
                                        read_shutter_dial(),now,&settings);
        if (action & (C3_NAV_MODE_OPEN | C3_NAV_MENU_OPEN)) {
#if C3_STARTUP_HINT
            c3_startup_hint_dismiss(&startup_hint);
#endif
            c3_camera_preview_stop();
            if (action & C3_NAV_MENU_OPEN) {
                c3_menu_draw_settings(&nav,&settings);
            } else {
                c3_menu_draw_mode(&exposure);
#if C3_FUNCTION_MENU
                battery_refresh_tick=HAL_GetTick();
#endif
            }
            continue;
        }
        uint16_t suppressed_up = (uint16_t)(key.released & suppress_preview_keys);
#if C3_STARTUP_HINT
        if(key.pressed & C3_BTN_SHUTTER)c3_startup_hint_dismiss(&startup_hint);
#endif
        key.pressed &= (uint16_t)~suppress_preview_keys;
        key.short_released &= (uint16_t)~suppress_preview_keys;
        key.long_pressed &= (uint16_t)~suppress_preview_keys;
        key.long_released &= (uint16_t)~suppress_preview_keys;
        suppress_preview_keys &= (uint16_t)~suppressed_up;
        uint8_t old_effective_dial = c3_exposure_effective_dial(&exposure);
        uint8_t old_effective_gain = c3_exposure_effective_gain(&exposure);
        (void)c3_exposure_poll_dial(&exposure,read_shutter_dial(),now);
        if (key.short_released & C3_BTN_ISO) c3_exposure_gain_next(&exposure);
        if (key.long_pressed & C3_BTN_ISO) {
            settings.peaking ^= 1u;
            settings_dirty = 1u;
            settings_change_tick = now;
            c3_focus_reset(&c3_focus_state);
            c3_score_reset(&c3_score_state);
        }
#if C3_ZOOM_AE_LOCK
        uint8_t exposure_changed=(uint8_t)(old_effective_dial!=c3_exposure_effective_dial(&exposure)||
                                         old_effective_gain!=c3_exposure_effective_gain(&exposure));
        if(exposure_changed && preview_zoom4x){
            /* A new shutter/gain requires full-FoV metering; never meter
             * on the focus crop, or fire with the old exposure lock. */
            zoom_release();resume_preview(&camera_ok,&fps,&exposure,&meter);continue;
        }
        if(key.short_released&C3_BTN_4X){
            if(exposure_changed){resume_preview_controls(&camera_ok,&fps,&exposure,&meter);continue;}
            if(preview_zoom4x){zoom_release();}
            else {
                uint8_t warning=meter.valid&&meter.attempted&&
                    (uint32_t)(HAL_GetTick()-meter.last_attempt_ms)<3u*C3_METER_SAMPLE_MS?meter.warning:0u;
                c3_camera_preview_stop();
                if(c3_zoom_hold_read_synced(&zoom_hold,&exposure,ov7725_read_reg_checked,ov7725_write_reg,
                                           c3_camera_wait_zoom_frame)){
                    zoom_hold.warning=warning;preview_zoom4x=1u;
                    c3_exposure_set_preview_zoom(1u);c3_camera_preview_select_zoom(1u);
                }else{
                    zoom_release();c3_ui_message("4X LOCK FAIL");
#if C3_ZOOM_LOCK_DIAG
                    zoom_diag_failure(c3_zoom_read_failure);
#else
                    HAL_Delay(500u);
#endif
                }
            }
            resume_preview(&camera_ok,&fps,&exposure,&meter);
            continue; /* consume same-batch shutter; first frame after switch not a shot */
        }
#else
        if (key.short_released & C3_BTN_4X) {
            /* The OV7725, not the STM32, now performs the centre crop.
             * Reinitialize RGB565 preview to change VGA/QVGA sensor mode;
             * never change the 644x480 RAW8 DNG capture geometry. */
            preview_zoom4x ^= 1u;
#if C3_SHUTTER_DATASHEET_TIMING
            c3_exposure_set_preview_zoom(preview_zoom4x);
#endif
            c3_camera_preview_select_zoom(preview_zoom4x);
            resume_preview(&camera_ok, &fps, &exposure, &meter);
            if (!camera_ok) continue;
        }
        /* Long-press 4X has no shortcut. FPS and monochrome live only in
         * SETTINGS, where accidental changes are less likely. */
        uint8_t exposure_changed = (uint8_t)(
            old_effective_dial != c3_exposure_effective_dial(&exposure) ||
            old_effective_gain != c3_exposure_effective_gain(&exposure));
#endif
        if (exposure_changed && !(key.pressed & C3_BTN_SHUTTER)) {
            /* Previous code called full ov7725_init() here (~460ms). */
            resume_preview_controls(&camera_ok,&fps,&exposure,&meter);
            if (!camera_ok) continue;
        }
#if !C3_FAST_BMP_ENABLE && !C3_RAW_LOCK_EXPOSURE
        if (key.pressed & C3_BTN_SHUTTER) {
            c3_ui_message("CAPTURE");
            pending_format = (c3_output_format_t)settings.output_format;
            if (pending_format == C3_OUTPUT_DNG) {
#if C3_ZOOM_AE_LOCK
                if(zoom_hold.source.valid)(void)c3_dng_capture_locked_awb(zoom_hold.red,zoom_hold.green,zoom_hold.blue);
                else
#endif
                    (void)c3_dng_capture_preview_awb(ov7725_read_reg_checked);
            }
            uint8_t sensor_ready = pending_format == C3_OUTPUT_DNG ?
                                   c3_camera_raw_init() : c3_camera_bmp_init();
            uint8_t exposure_ready = sensor_ready ?
                (c3_exposure_prepare_timing(&exposure,
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
                     ov7725_read_reg_checked,ov7725_write_reg) &&
                 c3_exposure_apply(&exposure,
#if C3_SHUTTER_DATASHEET_TIMING
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
#else
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_PREVIEW,
#endif
                     ov7725_write_reg)) : 0u;
            if (!sensor_ready || !exposure_ready) {
                show_capture_init_error(pending_format, sensor_ready);
                HAL_Delay(2600u); /* time to photograph the error code */
            } else {
                /* Any 0xFF exception in RAW is a warning, not a verified
                 * Bayer mode. Show it BEFORE capturing or writing SD. */
                if (pending_format == C3_OUTPUT_DNG &&
                    c3_ov_verify_warn_mask != 0u) {
                    c3_ui_message("RAW SCCB UNVERIFIED");
                    HAL_Delay(850u);
                }
                /* Discard transitional frames after sensor reset / format change. */
                uint8_t ok = 1u;
                /* AEC/AGC cannot converge instantly after a sensor reset.
                 * Keep 6 settling frames whenever either control is auto. */
                unsigned settle = (c3_exposure_effective_dial(&exposure) == C3_EXPOSURE_AUTO_CODE ||
                                   c3_exposure_effective_gain(&exposure) == 0u) ? 6u : 2u;
                if (pending_format == C3_OUTPUT_DNG && settle < C3_DNG_SETTLE_LOCKED)
                    settle = C3_DNG_SETTLE_LOCKED;
#if C3_DNG_VSYNC_SETTLE
                if (pending_format == C3_OUTPUT_DNG) {
                    /* Full reset / no locked snapshot: conservative wait. */
                    ok = c3_camera_wait_still_frames(settle, 1200u);
                } else
#endif
                for (unsigned i = 0; i < settle; ++i) {
                    if (!c3_camera_capture(1200u)) { ok = 0u; break; }
                }
                if (ok && pending_format == C3_OUTPUT_DNG &&
                    !ov7725_verify_raw_blc_capture()) {
                    c3_camera_last_still_stage = 4u; /* post-settle BLC gate */
                    c3_camera_last_still_code = 4u;
                    show_capture_init_error(pending_format, 0u);
                    HAL_Delay(2600u);
                } else if (ok && c3_camera_capture(1200u) && c3_camera_raw_hold()) {
#if C3_DNG_BLACK_CALIBRATION
                    if (pending_format == C3_OUTPUT_DNG) {
                        uint8_t step=c3_exposure_effective_gain(&exposure);
                        uint8_t gain=step?c3_exposure_gain_reg(step):0xFFu;
                        c3_dng_capture_black_level(c3_exposure_effective_dial(&exposure),
                                                   gain,0u,ov7725_read_reg_checked);
                    }
#endif
                    c3_recovery_captured(&recovery);
                    c3_ui_message("SAVING");
                    c3_save_result_t result = c3_storage_save(pending_format);
                    c3_recovery_save_finished(&recovery, result);
                    sd_ok = (uint8_t)(result == C3_SAVE_OK);
                    if (result == C3_SAVE_OK) {
                        show_save_success();
                        c3_camera_raw_discard();
#if C3_POSTSHOT
                        postshot_used=postshot_wait(&settings,pending_format,&sd_ok);
#endif
                    } else {
                        show_pending_save_error(result);
                        c3_input_reset(read_buttons(), HAL_GetTick());
                    }
                } else {
                    c3_ui_message("FRAME TIMEOUT");
                    HAL_Delay(900u);
                }
            }
            /* On write failure DO NOT initialize RGB preview: it would
             * overwrite the retained DNG/BMP source frame. */
            sleep_last_activity=HAL_GetTick(); /* blocking capture/save is activity */
            if (!c3_recovery_has_frame(&recovery)) {
                /* Any key presses during exposure or SD writes must not
                 * trigger a second accidental shot on return to preview. */
                c3_input_reset(read_buttons(), HAL_GetTick());
                resume_preview(&camera_ok, &fps, &exposure, &meter);
#if C3_POSTSHOT
                if(postshot_used)postshot_resume_input(&nav,&suppress_preview_keys,&sleep_last_activity,&sleep_last_dial);
#endif
            }
        }
#else
        if (key.pressed & C3_BTN_SHUTTER) {
            pending_format = (c3_output_format_t)settings.output_format;
            /* Read AWB ONLY in live RGB565 preview. DNG generation and SD
             * retries reuse this frozen snapshot; RAW mode must not run AWB. */
            if (pending_format == C3_OUTPUT_DNG) {
#if C3_ZOOM_AE_LOCK
                if(zoom_hold.source.valid)(void)c3_dng_capture_locked_awb(zoom_hold.red,zoom_hold.green,zoom_hold.blue);
                else
#endif
                    (void)c3_dng_capture_preview_awb(ov7725_read_reg_checked);
            }
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
            raw_noreset_pending=0u;
            raw_restore_snapshot.valid=0u;
#endif
            /* BMP 1X can keep the exact live RGB565/AEC/AGC configuration.
             * 4X hardware crop needs a full-FOV still and stays on legacy path. */
            uint8_t fast_bmp = (uint8_t)(C3_FAST_BMP_ENABLE &&
                               pending_format == C3_OUTPUT_BMP && !preview_zoom4x && !exposure_changed);
            uint8_t fast_bmp_captured = 0u;
#if C3_RAW_LOCK_EXPOSURE
            c3_exposure_snapshot_t shot = {0};
            /* Read the ACTUAL AEC+GAIN before RAW reset destroys the preview
             * auto-exposure state. SCCB reads are outside FIFO->LCD pixels. */
            uint8_t raw_locked = (uint8_t)(pending_format == C3_OUTPUT_DNG &&
                !exposure_changed &&
#if C3_ZOOM_AE_LOCK
                (zoom_hold.source.valid ? (shot=zoom_hold.source,1u) :
#endif
                c3_exposure_snapshot_read(&shot, ov7725_read_reg_checked)
#if C3_ZOOM_AE_LOCK
                )
#endif
                );
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
            if (raw_locked) raw_restore_snapshot=shot;
#endif
#else
            uint8_t raw_locked = 0u;
#endif
            /* Shot-path label is diagnostic only; it adds no delay.
             * RAW LEGACY means SCCB snapshot failed or feature disabled. */
            c3_ui_message(pending_format == C3_OUTPUT_DNG ?
                           (raw_locked ?
                             (c3_dng_wb_sensor_valid() ? "RAW LOCK AWB" : "RAW LOCK FIX") :
                             (c3_dng_wb_sensor_valid() ? "RAW LEGACY AWB" : "RAW LEGACY FIX")) :
                           (fast_bmp ? "BMP FAST" : "CAPTURE"));
            uint8_t sensor_ready = pending_format == C3_OUTPUT_DNG ?
#if C3_FAST_RAW_SWITCH
                                   c3_camera_raw_fast_init() :
#else
                                   c3_camera_raw_init() :
#endif
                                   fast_bmp ? c3_camera_bmp_fast_init() : c3_camera_bmp_init();
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
            raw_noreset_pending=(uint8_t)(pending_format == C3_OUTPUT_DNG &&
                            sensor_ready && c3_camera_raw_fast_path_used);
#endif
            uint8_t exposure_ready = sensor_ready ?
#if C3_ZOOM_AE_LOCK
                (zoom_hold.source.valid ? c3_zoom_hold_apply(&zoom_hold,
                    pending_format==C3_OUTPUT_DNG?C3_EXPOSURE_RAW:C3_EXPOSURE_BMP,
                    ov7725_read_reg_checked,ov7725_write_reg) :
#endif
                (fast_bmp ? 1u :
                  (c3_exposure_prepare_timing(&exposure,
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
                     ov7725_read_reg_checked,ov7725_write_reg) &&
                   c3_exposure_apply(&exposure,
#if C3_SHUTTER_DATASHEET_TIMING
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
#else
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_PREVIEW,
#endif
                     ov7725_write_reg)))
#if C3_ZOOM_AE_LOCK
                )
#endif
                : 0u;
#if C3_RAW_LOCK_EXPOSURE
            if (exposure_ready && raw_locked && pending_format == C3_OUTPUT_DNG
#if C3_ZOOM_AE_LOCK
                && !zoom_hold.source.valid
#endif
                )
                exposure_ready = c3_exposure_apply_raw_locked_timed(&shot,&exposure,
                                                              C3_RAW_CLKRC,
                                                              ov7725_write_reg);
#endif
            if (!sensor_ready || !exposure_ready) {
                show_capture_init_error(pending_format, sensor_ready);
                HAL_Delay(2600u); /* time to photograph the error code */
            } else {
                /* Any 0xFF exception in RAW is a warning, not a verified
                 * Bayer mode. Show it BEFORE capturing or writing SD. */
                if (pending_format == C3_OUTPUT_DNG &&
                    c3_ov_verify_warn_mask != 0u) {
                    c3_ui_message("RAW SCCB UNVERIFIED");
                    HAL_Delay(850u);
                }
                /* The BMP 1X shortcut does not reset AEC/AGC, so no blind
                 * settling frames. RAW discards at least C3_DNG_SETTLE_LOCKED
                 * frames for format changes and automatic BLC settling. */
                uint8_t ok = 1u;
                unsigned settle = fast_bmp ? 0u : raw_locked ?
                    C3_DNG_SETTLE_LOCKED :
                    (c3_exposure_effective_dial(&exposure) == C3_EXPOSURE_AUTO_CODE ||
                     c3_exposure_effective_gain(&exposure) == 0u) ? 6u : 2u;
                if (pending_format == C3_OUTPUT_DNG && settle < C3_DNG_SETTLE_LOCKED)
                    settle = C3_DNG_SETTLE_LOCKED;
#if C3_DNG_VSYNC_SETTLE
                if (pending_format == C3_OUTPUT_DNG) {
#if C3_FAST_RAW_SWITCH
                    /* The timed lock reapplies the preview's actual gain.
                     * Never shorten a full-reset, unlocked, simultaneous
                     * control change, or SCCB-warning shot. */
                    if (c3_camera_raw_fast_path_used && raw_locked &&
                        !exposure_changed && c3_ov_verify_warn_mask == 0u)
                        settle = C3_DNG_FAST_SETTLE_FRAMES;
#endif
                    ok = c3_camera_wait_still_frames(settle, 1200u);
                } else
#endif
                for (unsigned i = 0; i < settle; ++i) {
                    if (!c3_camera_capture(1200u)) { ok = 0u; break; }
                }
                if (ok && pending_format == C3_OUTPUT_DNG &&
                    !ov7725_verify_raw_blc_capture()) {
                    c3_camera_last_still_stage = 4u; /* post-settle BLC gate */
                    c3_camera_last_still_code = 4u;
                    show_capture_init_error(pending_format, 0u);
                    HAL_Delay(2600u);
                } else if (ok && c3_camera_capture(1200u) && c3_camera_raw_hold()) {
                    fast_bmp_captured = fast_bmp;
#if C3_DNG_BLACK_CALIBRATION
                    if (pending_format == C3_OUTPUT_DNG) {
                        uint8_t dial=c3_exposure_effective_dial(&exposure);
                        uint8_t step=c3_exposure_effective_gain(&exposure);
                        uint8_t gain=step?c3_exposure_gain_reg(step):0xFFu;
                        uint16_t rows=0u;
#if C3_RAW_LOCK_EXPOSURE
                        if (raw_locked) {
                            gain=shot.gain_register;
                            if (dial==C3_EXPOSURE_AUTO_CODE)
                                rows=
#if C3_ZOOM_AE_LOCK
                                    zoom_hold.source.valid?c3_zoom_hold_rows(&zoom_hold,C3_EXPOSURE_RAW):
#endif
                                    c3_exposure_raw_rows(shot.preview_rows,C3_RAW_CLKRC);
                        }
#endif
                        c3_dng_capture_black_level(dial,gain,rows,ov7725_read_reg_checked);
                    }
#endif
                    c3_recovery_captured(&recovery);
                    c3_ui_message("SAVING");
                    c3_save_result_t result = c3_storage_save(pending_format);
                    c3_recovery_save_finished(&recovery, result);
                    sd_ok = (uint8_t)(result == C3_SAVE_OK);
                    if (result == C3_SAVE_OK) {
                        show_save_success();
                        c3_camera_raw_discard();
#if C3_POSTSHOT
                        postshot_used=postshot_wait(&settings,pending_format,&sd_ok);
#endif
                    } else {
                        show_pending_save_error(result);
                        c3_input_reset(read_buttons(), HAL_GetTick());
                    }
                } else {
                    c3_ui_message("FRAME TIMEOUT");
                    HAL_Delay(900u);
                }
            }
            /* On write failure DO NOT initialize RGB preview: it would
             * overwrite the retained DNG/BMP source frame. */
            sleep_last_activity=HAL_GetTick(); /* blocking capture/save is activity */
            if (!c3_recovery_has_frame(&recovery)) {
                /* Any key presses during exposure or SD writes must not
                 * trigger a second accidental shot on return to preview. */
                c3_input_reset(read_buttons(), HAL_GetTick());
                if (fast_bmp_captured) {
                    /* BMP did not change any sensor register: rearm only
                     * FIFO preview, with no sensor reset or exposure loss. */
                    c3_camera_preview_rearm();
                    c3_fps_reset(&fps);
                    c3_focus_reset(&c3_focus_state);
                    c3_score_reset(&c3_score_state);
                    c3_meter_reset(&meter);
                } else {
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
                    resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                             &raw_restore_snapshot,
                                             raw_noreset_pending);
                    raw_noreset_pending=0u;
#else
                    resume_preview(&camera_ok, &fps, &exposure, &meter);
#endif
                }
#if C3_POSTSHOT
                if(postshot_used)postshot_resume_input(&nav,&suppress_preview_keys,&sleep_last_activity,&sleep_last_dial);
#endif
            }
        }
#endif

    }
#endif /* C3_PREVIEW_PROBE */
}
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc={0};
    RCC_ClkInitTypeDef clk={0};
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE;
    osc.HSEState=RCC_HSE_ON;
    osc.HSEPredivValue=RCC_HSE_PREDIV_DIV1;
    osc.HSIState=RCC_HSI_ON;
    osc.PLL.PLLState=RCC_PLL_ON;
    osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL=RCC_PLL_MUL9;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK) Error_Handler();
    clk.ClockType=RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                  RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV2;
    clk.APB2CLKDivider=RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_2)!=HAL_OK) Error_Handler();
}
void Error_Handler(void)
{
    /* Keep interrupts enabled for debugger/SysTick. Do not touch FLASH. */
    while(1) { __NOP(); }
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file,uint32_t line) { (void)file; (void)line; }
#endif

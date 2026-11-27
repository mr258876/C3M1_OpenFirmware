/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* C3M1 OpenFirmware: application, capture and menu state machines. */
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
#include "c3_postshot.h"
#include "c3_osd.h"
#include "c3_battery.h"
#include "c3_sleep.h"
#include "c3_settings.h"
#include "c3_exposure.h"
#include "c3_boot_timing.h"
#include "c3_startup_hint.h"
extern volatile uint32_t c3_live_errors;
#include "c3_meter.h"
#include "../../Drivers/BSP/OV7725/ov7725.h"
#include "../../Drivers/BSP/OV7725/sccb.h"
#include "../../Drivers/SYSTEM/delay/delay.h"

/* LCD and sensor power-settle windows overlap at boot.
 * SCCB uses a 50 us edge delay for reliable register access. */

/* Actual production schematic: PF1 ISO, PF2 SHOT, PF3 4X, PF10 FILM. */
#define BUTTONS C3_BTN_MASK

void SystemClock_Config(void);
/* Watch c3_boot_stage through SWD if a full-black boot ever recurs.
 * 1=GPIO, 2=LCD ready, 3=CAM init, 4=CAM done, 5=SD init, 6=loop. */
volatile uint32_t c3_boot_stage;
volatile uint32_t c3_cam_init_first_code;
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

static void show_camera_error(void)
{
    switch (c3_camera_last_init_stage) {
        case 1u:
            c3_ui_message(c3_camera_last_sensor_code == 4u ?
                           "CAM VERIFY ERR" : "CAM SENSOR ERR");
            show_sensor_detail();
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


/* The save transaction has already been synced, closed and renamed before
 * this path; no success banner or UI-only delay is needed. Keep an explicit
 * 0 fallback to make a byte-identical v0.1.67 rollback build possible. */
static void show_save_success(void)
{
}

/* The previous RGB preview is still in LCD GRAM. After a successful SD
 * retry, erase only the old recovery panel (not the entire preview). */
static void postshot_prepare_retry_background(const c3_settings_t *settings)
{
    if(c3_settings_wind(settings)&&!c3_settings_review(settings))
        c3_lcd_rect(12,76,296,113,0x0000u);
}
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
                    c3_i18n_is_chinese() ? 129 :
                    126, "CHECK SD TMP FILE", 0xFFE0u, 1);
    c3_ui_hint(22,144,C3_HINT_SHOT,"RETRY",0xFFFFu);
    c3_ui_hint(22,162,C3_HINT_FILM,"HOLD - DISCARD",0xFFFFu);
}

static uint8_t preview_zoom4x;
static c3_zoom_hold_t zoom_hold;
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

static void resume_preview(uint8_t *camera_ok, c3_fps_t *fps,
                           const c3_exposure_t *exposure, c3_meter_t *meter)
{
    *camera_ok = c3_camera_preview_init();
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
        HAL_Delay(500u);
        *camera_ok=c3_camera_preview_init();
        if(*camera_ok){c3_camera_preview_stop();if(!zoom_apply_preview(exposure)){
            *camera_ok=0u;c3_camera_last_init_stage=3u;
        }}
    }
    if (*camera_ok) c3_camera_preview_rearm();
    c3_fps_reset(fps);
    c3_meter_reset(meter);
    if(*camera_ok)zoom_meter_seed(meter);
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
    if (*camera_ok) {
        c3_camera_preview_stop();
        if (
            zoom_apply_preview(exposure)
            ) {
            c3_camera_preview_rearm();
            c3_fps_reset(fps);
            c3_meter_reset(meter);
            c3_focus_reset(&c3_focus_state);
            c3_score_reset(&c3_score_state);
            return;
        }
    }
    /* Invalid settings / stale sensor: use the known working recovery. */
    resume_preview(camera_ok,fps,exposure,meter);
}

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
        if(zoom_hold.source.valid){
            if(zoom_apply_preview(exposure)){
                *camera_ok=1u;c3_camera_preview_rearm();c3_fps_reset(fps);c3_meter_reset(meter);
                zoom_meter_seed(meter);c3_focus_reset(&c3_focus_state);c3_score_reset(&c3_score_state);
                ++c3_rgb_fast_restores;return;
            }
            ++c3_rgb_fast_restore_fallbacks;
            resume_preview(camera_ok,fps,exposure,meter);return;
        }
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
            zoom_apply_preview(exposure)
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
    zoom_release(); /* wake starts a fresh 1X metering session */
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



int main(void)
{
    HAL_Init();
    SystemClock_Config();
    delay_init(72);
    MX_GPIO_Init();
    /* Explicitly hold LCD in reset with its backlight OFF while power
     * settles. gpio.c preloads these LOW *before* enabling output mode. */
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN | C3_LCD_RESET_PIN, GPIO_PIN_RESET);
    c3_boot_stage = 1u;
    /* MX_GPIO_Init has released SCL/SDA high in open-drain mode.
     * LCD RESET remains low; FIFO WEN remains off. */
    const uint32_t cam_power_tick=HAL_GetTick();
    /* This is a read-only operation; defaults to 180 degrees on erased Flash. */
    c3_settings_t settings;
    (void)c3_settings_flash_load(&settings);
    c3_i18n_set_language(c3_settings_language(&settings));
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
    if(c3_settings_language_pending(&settings)) {
        (void)choose_language(&settings,1u);
        c3_lcd_fill(0u);
        c3_ui_message("C3M1 BOOT");
    }

    /* Initialize camera before the SD stack. Diagnostic v0.1 proved SCCB,
     * exposure and 0x0F VSYNC/s work without mounting a card; this order
     * differentiates camera failures from SD startup hangs. */
    c3_meter_t meter;
    c3_meter_reset(&meter);
    c3_exposure_t exposure;
    c3_exposure_init(&exposure, read_shutter_dial(), HAL_GetTick());
    c3_boot_stage = 3u;
    /* Reproduce the working diagnostic firmware's power-on timing and
     * initial reset-before-init sequence. This is a hardware hypothesis,
     * not proof that power-up timing caused the original failure. */
    c3_ui_message("CAM WARMUP");
    const uint32_t cam_wait=c3_boot_remaining_ms(cam_power_tick,HAL_GetTick(),C3_CAM_POWER_SETTLE_MS);
    if(cam_wait)HAL_Delay(cam_wait);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(5u);
    c3_sccb_half_period_us = C3_SCCB_MAIN_HALF_US; /* v146: default 50us, optional 100us */
    sccb_init();
    (void)ov7725_write_reg(0x12u,0x80u);
    HAL_Delay(60u);
    c3_ui_message("CAM INIT");
    uint8_t camera_ok = c3_camera_preview_init();
    c3_cam_init_first_code = camera_ok ? 0u : c3_camera_last_sensor_code;
    if (!camera_ok && c3_camera_last_init_stage == 1u) {
        /* One bounded retry only; leave detailed failure data on LCD.
         * Never loop indefinitely or pretend the sensor is healthy. */
        HAL_Delay(150u);
        camera_ok = c3_camera_preview_init();
    }
    if(camera_ok)c3_camera_preview_stop();
    if(camera_ok && !zoom_apply_preview(&exposure)){
        camera_ok = 0u;
        c3_camera_last_init_stage = 3u;
    }
    if (camera_ok) c3_camera_preview_stop(); /* no capture while mounting SD */
    c3_boot_stage = 4u;
    if (camera_ok) c3_ui_message("CAM READY");
    else show_camera_error();

    c3_boot_stage = 5u;
    c3_ui_message("SD INIT");
    MX_SDIO_SD_Init();
    MX_FATFS_Init();
    uint8_t sd_ok = c3_storage_mount();
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
    c3_ui_nav_t nav;
    c3_playback_t playback={0};
    c3_fps_t fps;
    c3_capture_recovery_t recovery;
    c3_output_format_t pending_format = C3_OUTPUT_DNG;
    /* Must survive the SD write error / retry / explicit-discard menu. */
    uint8_t raw_noreset_pending=0u;
    c3_exposure_snapshot_t raw_restore_snapshot={0};
    c3_recovery_reset(&recovery);
    c3_input_reset(read_buttons(), HAL_GetTick());
    c3_ui_nav_reset(&nav, read_shutter_dial(), HAL_GetTick());
    c3_fps_reset(&fps);
    c3_focus_reset(&c3_focus_state);
    c3_score_reset(&c3_score_state);
    if (!camera_ok) show_camera_error();
    c3_boot_stage = 6u;

    uint32_t retry_tick = HAL_GetTick();
    /* First boot language selection has already completed above; the hint
     * starts only after the first successfully rendered preview frame. */
    c3_startup_hint_t startup_hint={0};
    while (1) {
        /* One cheap GPIO update per foreground iteration, never per pixel.
         * Sleeps are handled by TIM2 and SD_write itself lights the LED. */
        c3_sd_led_poll(HAL_GetTick());
        uint8_t postshot_used=0u;
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
                    postshot_prepare_retry_background(&settings);
                    postshot_used=postshot_wait(&settings,pending_format,&sd_ok);
                    resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                             &raw_restore_snapshot,
                                             raw_noreset_pending);
                    raw_noreset_pending=0u;
                    c3_input_reset(read_buttons(), HAL_GetTick());
                    if(postshot_used)postshot_resume_input(&nav,&suppress_preview_keys,&sleep_last_activity,&sleep_last_dial);
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
                resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                         &raw_restore_snapshot,raw_noreset_pending);
                raw_noreset_pending=0u;
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
                    c3_i18n_set_language(c3_settings_language(&settings));
                    zoom_release();
                    /* Defaults, including pending language, are already
                     * committed. Power loss here requires the picker next boot. */
                    if (choose_language(&settings,1u))
                        c3_startup_hint_arm(&startup_hint);
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
            if (change & C3_NAV_ABOUT_OPEN) {
                c3_menu_draw_about();
                continue;
            }
            if (change & C3_NAV_ABOUT_BACK) {
                c3_menu_draw_settings(&nav,&settings);
                continue;
            }
            if (change & C3_NAV_EV_ADJUST)
                (void)c3_exposure_ev_adjust(&exposure,nav.ev_step_delta);
            if (change & C3_NAV_EV_ADJUST) {
                c3_menu_draw_mode(&exposure);
                battery_refresh_tick=HAL_GetTick();
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
            if (nav.page == C3_UI_MODE &&
                (uint32_t)(HAL_GetTick()-battery_refresh_tick)>=2000u) {
                c3_menu_draw_battery();
                battery_refresh_tick=HAL_GetTick();
            }
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
            uint32_t boot_errors_before=c3_live_errors;
            c3_camera_preview_draw(&settings, preview_zoom4x, sd_ok,
                                   c3_storage_next_number(),fps.tenths,
                                   (uint8_t)exposure.shoot_mode,
                                   c3_exposure_effective_dial(&exposure),
                                   c3_exposure_effective_gain(&exposure),
                                   exposure.dial_code, exposure.ev_half_steps,
                                   c3_exposure_ev_active(&exposure), &meter);
            c3_camera_preview_release();
            if(c3_startup_hint_frame(&startup_hint,c3_settings_startup_hint(&settings),
                                     boot_errors_before==c3_live_errors,
                                     HAL_GetTick()))c3_ui_draw_startup_hint();
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
                if(zoom_hold.source.valid)meter.warning=zoom_hold.warning;
                else
                {
                    uint8_t aew,aeb;
                    c3_exposure_ev_targets(exposure.ev_half_steps,&aew,&aeb,0);
                    c3_meter_update_warning(&meter,(uint8_t)exposure.shoot_mode,
                        aeb,aew,ov7725_read_reg_checked);
                }
            }
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
            c3_startup_hint_dismiss(&startup_hint);
            c3_camera_preview_stop();
            if (action & C3_NAV_MENU_OPEN) {
                c3_menu_draw_settings(&nav,&settings);
            } else {
                c3_menu_draw_mode(&exposure);
                battery_refresh_tick=HAL_GetTick();
            }
            continue;
        }
        uint16_t suppressed_up = (uint16_t)(key.released & suppress_preview_keys);
        if(key.pressed & C3_BTN_SHUTTER)c3_startup_hint_dismiss(&startup_hint);
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
                    HAL_Delay(500u);
                }
            }
            resume_preview(&camera_ok,&fps,&exposure,&meter);
            continue; /* consume same-batch shutter; first frame after switch not a shot */
        }
        if (exposure_changed && !(key.pressed & C3_BTN_SHUTTER)) {
            /* Previous code called full ov7725_init() here (~460ms). */
            resume_preview_controls(&camera_ok,&fps,&exposure,&meter);
            if (!camera_ok) continue;
        }
        if (key.pressed & C3_BTN_SHUTTER) {
            pending_format = (c3_output_format_t)settings.output_format;
            /* Read AWB ONLY in live RGB565 preview. DNG generation and SD
             * retries reuse this frozen snapshot; RAW mode must not run AWB. */
            if (pending_format == C3_OUTPUT_DNG) {
                if(zoom_hold.source.valid)(void)c3_dng_capture_locked_awb(zoom_hold.red,zoom_hold.green,zoom_hold.blue);
                else
                    (void)c3_dng_capture_preview_awb(ov7725_read_reg_checked);
            }
            raw_noreset_pending=0u;
            raw_restore_snapshot.valid=0u;
            /* BMP 1X can keep the exact live RGB565/AEC/AGC configuration.
             * 4X hardware crop needs a full-FOV still and stays on legacy path. */
            uint8_t fast_bmp = (uint8_t)(C3_FAST_BMP_ENABLE &&
                               pending_format == C3_OUTPUT_BMP && !preview_zoom4x && !exposure_changed);
            uint8_t fast_bmp_captured = 0u;
            c3_exposure_snapshot_t shot = {0};
            /* Read the ACTUAL AEC+GAIN before RAW reset destroys the preview
             * auto-exposure state. SCCB reads are outside FIFO->LCD pixels. */
            uint8_t raw_locked = (uint8_t)(pending_format == C3_OUTPUT_DNG &&
                !exposure_changed &&
                (zoom_hold.source.valid ? (shot=zoom_hold.source,1u) :
                c3_exposure_snapshot_read(&shot, ov7725_read_reg_checked)
                )
                );
            if (raw_locked) raw_restore_snapshot=shot;
            /* Shot-path label is diagnostic only; it adds no delay.
             * RAW LEGACY means SCCB snapshot failed or feature disabled. */
            c3_ui_message(pending_format == C3_OUTPUT_DNG ?
                           (raw_locked ?
                             (c3_dng_wb_sensor_valid() ? "RAW LOCK AWB" : "RAW LOCK FIX") :
                             (c3_dng_wb_sensor_valid() ? "RAW LEGACY AWB" : "RAW LEGACY FIX")) :
                           (fast_bmp ? "BMP FAST" : "CAPTURE"));
            uint8_t sensor_ready = pending_format == C3_OUTPUT_DNG ?
                                   c3_camera_raw_fast_init() :
                                   fast_bmp ? c3_camera_bmp_fast_init() : c3_camera_bmp_init();
            raw_noreset_pending=(uint8_t)(pending_format == C3_OUTPUT_DNG &&
                            sensor_ready && c3_camera_raw_fast_path_used);
            uint8_t exposure_ready = sensor_ready ?
                (zoom_hold.source.valid ? c3_zoom_hold_apply(&zoom_hold,
                    pending_format==C3_OUTPUT_DNG?C3_EXPOSURE_RAW:C3_EXPOSURE_BMP,
                    ov7725_read_reg_checked,ov7725_write_reg) :
                (fast_bmp ? 1u :
                  (c3_exposure_prepare_timing(&exposure,
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
                     ov7725_read_reg_checked,ov7725_write_reg) &&
                   c3_exposure_apply(&exposure,
                     pending_format == C3_OUTPUT_DNG ? C3_EXPOSURE_RAW : C3_EXPOSURE_BMP,
                     ov7725_write_reg)))
                )
                : 0u;
            if (exposure_ready && raw_locked && pending_format == C3_OUTPUT_DNG
                && !zoom_hold.source.valid
                )
                exposure_ready = c3_exposure_apply_raw_locked_timed(&shot,&exposure,
                                                              C3_RAW_CLKRC,
                                                              ov7725_write_reg);
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
                if (pending_format == C3_OUTPUT_DNG) {
                    /* The timed lock reapplies the preview's actual gain.
                     * Never shorten a full-reset, unlocked, simultaneous
                     * control change, or SCCB-warning shot. */
                    if (c3_camera_raw_fast_path_used && raw_locked &&
                        !exposure_changed && c3_ov_verify_warn_mask == 0u)
                        settle = C3_DNG_FAST_SETTLE_FRAMES;
                    ok = c3_camera_wait_still_frames(settle, 1200u);
                } else
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
                    if (pending_format == C3_OUTPUT_DNG) {
                        uint8_t dial=c3_exposure_effective_dial(&exposure);
                        uint8_t step=c3_exposure_effective_gain(&exposure);
                        uint8_t gain=step?c3_exposure_gain_reg(step):0xFFu;
                        uint16_t rows=0u;
                        if (raw_locked) {
                            gain=shot.gain_register;
                            if (dial==C3_EXPOSURE_AUTO_CODE)
                                rows=
                                    zoom_hold.source.valid?c3_zoom_hold_rows(&zoom_hold,C3_EXPOSURE_RAW):
                                    c3_exposure_raw_rows(shot.preview_rows,C3_RAW_CLKRC);
                        }
                        c3_dng_capture_black_level(dial,gain,rows,ov7725_read_reg_checked);
                    }
                    c3_recovery_captured(&recovery);
                    c3_ui_message("SAVING");
                    c3_save_result_t result = c3_storage_save(pending_format);
                    c3_recovery_save_finished(&recovery, result);
                    sd_ok = (uint8_t)(result == C3_SAVE_OK);
                    if (result == C3_SAVE_OK) {
                        show_save_success();
                        c3_camera_raw_discard();
                        postshot_used=postshot_wait(&settings,pending_format,&sd_ok);
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
                    resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,
                                             &raw_restore_snapshot,
                                             raw_noreset_pending);
                    raw_noreset_pending=0u;
                }
                if(postshot_used)postshot_resume_input(&nav,&suppress_preview_keys,&sleep_last_activity,&sleep_last_dial);
            }
        }

    }
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

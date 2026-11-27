#include "c3_camera.h"
#include "c3_lcd.h"
#include "c3_lcd_dma.h"
#include "c3_osd.h"
#include "c3_exposure.h"
#include "c3_focus.h"
#include "c3_preview_score.h"
#include "c3_peaking.h"
static c3_peak_exp_t peak_exp;
#include "c3_zoom.h"
#include "main.h"
#include "../../Drivers/BSP/OV7725/ov7725.h"

#include "c3_frame_sched.h"
#include "c3_fifo_dma_live_plan.h"

_Static_assert(C3_LCD_W == C3_ZOOM_OUT_W &&
               C3_LCD_HEIGHT == C3_ZOOM_OUT_H,
               "Preview geometry must match the native sensor output");

/* GPIO board map (verified schematic): WEN PB3, RCLK PB4, /OE PG15. */
/* VSYNC IRQ is the *only* producer of FIFO-frame completion events.
 * Foreground is the only consumer. IRQ-masked sections below are a handful
 * of instructions; IRQs remain enabled throughout pixel streaming.
 */
static c3_frame_sched_t frame_sched;
static uint8_t preview_read_pointer_initialized;
/* Set only after the final RAW frame has been captured and WEN stopped. */
static uint8_t raw_frame_held; /* Holds either RAW8 or RGB565 still bytes. */
static uint8_t preview_sensor_zoom4x;
volatile uint32_t c3_camera_last_init_stage;
volatile uint32_t c3_camera_last_sensor_code;
/* Last still-format init result: preserved across the next preview reset.
 * 0=ok; stage 1=sensor table, 2=BMP window, 3=FIFO still rearm. */
volatile uint32_t c3_camera_last_still_code;
volatile uint32_t c3_camera_last_still_stage;
volatile uint32_t c3_camera_pid_good_of_5;
volatile uint32_t c3_camera_pid_last;
volatile uint32_t c3_camera_vsync_edges;
volatile uint32_t c3_camera_vsync_last_ms;

static uint32_t irq_lock(void)
{
    uint32_t was_masked = __get_PRIMASK();
    __disable_irq();
    return was_masked;
}
static void irq_unlock(uint32_t was_masked)
{
    if ((was_masked & 1u) == 0u) __enable_irq();
}

/* WCK is driven by sensor PCLK on the production PCB. AL422B samples WRST
 * on WCK edges, so holding WRST low for a short window *outside* the EXTI
 * ISR is much safer than two NOPs inside an IRQ.
 */
static void write_pointer_reset(void)
{
    GPIOB->BRR = GPIO_PIN_3; /* WEN off, HREF gate blocks /WE */
    GPIOD->BRR = GPIO_PIN_6; /* /WRST = 0 */
    HAL_Delay(1u);           /* > one sensor PCLK period */
    GPIOD->BSRR = GPIO_PIN_6;
}

static void scheduler_stop(void)
{
    uint32_t ps = irq_lock();
    GPIOB->BRR = GPIO_PIN_3;
    c3_sched_reset(&frame_sched, C3_SCHED_OFF);
    irq_unlock(ps);
}

/* EXTI9_5 -> HAL_GPIO_EXTI_Callback -> this bounded handler. WEN is driven
 * before and after advancing the schedule. No sensor I2C or HAL GPIO call
 * inside the interrupt. Do NOT reset the write pointer at every VSYNC in
 * pipelined mode; it must move continuously around the AL422B ring.
 */
void c3_camera_vsync_isr(void)
{
    uint32_t tick = HAL_GetTick();
    c3_camera_vsync_last_ms = tick;
    ++c3_camera_vsync_edges;
    GPIOB->BRR = GPIO_PIN_3;
    if (c3_sched_vsync(&frame_sched)) GPIOB->BSRR = GPIO_PIN_3;
}

/* Confirm the device ID again after the entire register table has been
 * programmed. Majority-of-five filters isolated SCCB read glitches,. */
static uint8_t post_init_pid_ok(void)
{
    c3_camera_pid_good_of_5 = 0u;
    c3_camera_pid_last = 0xFFFFu;
    for (uint8_t i = 0; i < 5u; ++i) {
        uint16_t pid = (uint16_t)(((uint16_t)ov7725_read_reg(0x0Au) << 8) |
                                   ov7725_read_reg(0x0Bu));
        c3_camera_pid_last = pid;
        if (pid == OV7725_PID) ++c3_camera_pid_good_of_5;
        HAL_Delay(3u);
    }
    return c3_camera_pid_good_of_5 >= 3u;
}

/* Require two consecutive VSYNC IRQ edges with a plausible gap. This tests
 * PA8 interrupts after changing sensor registers. The timeout is bounded. */
static uint8_t wait_for_vsync(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t last_count = c3_camera_vsync_edges;
    uint32_t last_edge_ms = 0u;
    uint8_t observed = 0u;
    while ((uint32_t)(HAL_GetTick() - start) < timeout_ms) {
        uint32_t count = c3_camera_vsync_edges;
        if (count != last_count) {
            uint32_t stamp = c3_camera_vsync_last_ms;
            last_count = count;
            if (observed == 0u) {
                last_edge_ms = stamp;
                observed = 1u;
            } else {
                uint32_t period = stamp - last_edge_ms;
                if (period >= 5u && period <= 800u) return 1u;
                last_edge_ms = stamp;
            }
        }
        HAL_Delay(1u);
    }
    return 0u;
}

void c3_camera_preview_select_zoom(uint8_t zoom4x)
{
    preview_sensor_zoom4x = (uint8_t)(zoom4x != 0u);
}

uint8_t c3_camera_preview_init(void)
{
    scheduler_stop();
    raw_frame_held = 0u;
    preview_read_pointer_initialized = 0u;
    c3_camera_last_init_stage = 0u;
    uint8_t r = ov7725_init();
    if (r != 0u) {
        c3_camera_last_sensor_code = r;
        c3_camera_last_init_stage = 1u;
        return 0u;
    }
    r = ov7725_preview_window_set(preview_sensor_zoom4x);
    if (r != 0u) {
        c3_camera_last_sensor_code = r;
        c3_camera_last_init_stage = 2u;
        return 0u;
    }
    /* Nonfatal post-init PID observation: the first ID validation in
     * ov7725_init() is still mandatory, while post-table reads have been
     * seen to return stale data on this real board. Keep diagnosing that. */
    if (!post_init_pid_ok()) c3_ov_verify_warn_mask |= C3_OV_WARN_PID_POST;
    HAL_Delay(30u);
    c3_camera_preview_rearm();
    if (!wait_for_vsync(1600u)) {
        c3_camera_last_sensor_code = 7u; /* No valid VSYNC period */
        c3_camera_last_init_stage = 4u;
        scheduler_stop();
        return 0u;
    }
    c3_camera_last_sensor_code = 0u;
    return 1u;
}

void c3_camera_preview_stop(void)
{
    scheduler_stop();
    OV7725_OE(1);
    preview_read_pointer_initialized = 0u;
}

/* Lock snapshot settling in stopped PREVIEW, not STILL mode. ISR keeps
 * global edge count running even with FIFO WEN off. First fresh edge starts
 * a complete interval; second ends it. Do not clear/rearm the FIFO here.
 * Absolute timeout allows slow Night Mode, but cannot wait forever. */
uint8_t c3_camera_wait_zoom_frame(void)
{
    uint32_t ps=irq_lock();
    if(raw_frame_held || frame_sched.mode!=C3_SCHED_OFF){irq_unlock(ps);return 0u;}
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    uint32_t first=c3_camera_vsync_edges;
    irq_unlock(ps);
    uint32_t start=HAL_GetTick();
    while((uint32_t)(HAL_GetTick()-start)<2200u){
        if((uint32_t)(c3_camera_vsync_edges-first)>=2u)return 1u;
        HAL_Delay(1u);
    }
    return 0u;
}

void c3_camera_preview_rearm(void)
{
    scheduler_stop();
    preview_read_pointer_initialized = 0u;
    GPIOB->BRR = GPIO_PIN_3;
    write_pointer_reset();
    uint32_t ps = irq_lock();
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    c3_sched_reset(&frame_sched, C3_SCHED_PREVIEW);
    irq_unlock(ps);
}

/* Arm one-shot capture with the selected sensor mode. Explicitly reset
 * the FIFO address before arm, just like the RAW8 path. */
static uint8_t still_rearm_after_sensor_init(void)
{
    GPIOB->BRR = GPIO_PIN_3;
    HAL_Delay(30u);
    write_pointer_reset();
    uint32_t ps = irq_lock();
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    c3_sched_reset(&frame_sched, C3_SCHED_STILL);
    irq_unlock(ps);
    return 1u;
}

uint8_t c3_camera_bmp_fast_init(void)
{
    /* One full VSYNC-bounded capture without any SCCB writes, sensor reset,
     * exposure re-application or blind AEC settling delay.  The subsequent
     * c3_camera_capture() still resets the FIFO pointer and waits for both
     * start and end VSYNC edges. Never use this in 4X crop preview. */
    scheduler_stop();
    raw_frame_held = 0u;
    preview_read_pointer_initialized = 0u;
    OV7725_OE(1);
    c3_camera_last_still_stage = 0u;
    c3_camera_last_still_code = 0u;
    uint32_t ps = irq_lock();
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    c3_sched_reset(&frame_sched, C3_SCHED_STILL);
    irq_unlock(ps);
    return 1u;
}

uint8_t c3_camera_bmp_init(void)
{
    scheduler_stop();
    raw_frame_held = 0u;
    c3_camera_last_still_stage = 0u;
    c3_camera_last_still_code = 0u;
    /* Do not apply 4X preview crop to a saved BMP: full field of view. */
    uint8_t code = ov7725_init();
    if (code != 0u) {
        c3_camera_last_still_stage = 1u;
        c3_camera_last_still_code = code;
        return 0u;
    }
    code = ov7725_preview_window_set(0u);
    if (code != 0u) {
        c3_camera_last_still_stage = 2u;
        c3_camera_last_still_code = code;
        return 0u;
    }
    if (!still_rearm_after_sensor_init()) {
        c3_camera_last_still_stage = 3u;
        c3_camera_last_still_code = 1u;
        return 0u;
    }
    return 1u;
}

uint8_t c3_camera_raw_init(void)
{
    scheduler_stop();
    raw_frame_held = 0u;
    c3_camera_last_still_stage = 0u;
    c3_camera_last_still_code = 0u;
    uint8_t code = ov7725_RAW_init();
    if (code != 0u) {
        c3_camera_last_still_stage = 1u;
        c3_camera_last_still_code = code;
        return 0u;
    }
    if (!still_rearm_after_sensor_init()) {
        c3_camera_last_still_stage = 3u;
        c3_camera_last_still_code = 1u;
        return 0u;
    }
    return 1u;
}

/* v166 bench experiment: ONLY from a live, initialized RGB565 preview.
 * Faster sensor switch uses a differential RAW register table; strict key
 * register checks fail closed into the exact legacy full-reset RAW init.
 * It deliberately does NOT change the restore-to-preview path. */
volatile uint32_t c3_camera_raw_fast_path_used;
volatile uint32_t c3_camera_raw_fast_fallbacks;
uint8_t c3_camera_raw_fast_init(void)
{
    scheduler_stop();
    raw_frame_held = 0u;
    c3_camera_last_still_stage = 0u;
    c3_camera_last_still_code = 0u;
    c3_camera_raw_fast_path_used = 0u;
    uint8_t code = ov7725_RAW_fast_init();
    if (code != 0u) {
        ++c3_camera_raw_fast_fallbacks;
        /* Sensor state may be partial; only a full RESET + complete table
         * may recover it. Leave still capture disabled until it succeeds. */
        code = ov7725_RAW_init();
    } else {
        c3_camera_raw_fast_path_used = 1u;
    }
    if (code != 0u) {
        c3_camera_last_still_stage = 1u;
        c3_camera_last_still_code = code;
        return 0u;
    }
    if (!still_rearm_after_sensor_init()) {
        c3_camera_last_still_stage = 3u;
        c3_camera_last_still_code = 1u;
        return 0u;
    }
    return 1u;
}
uint8_t c3_camera_rgb_fast_restore(void)
{
    scheduler_stop();
    raw_frame_held=0u;
    preview_read_pointer_initialized=0u;
    uint8_t code=ov7725_RGB_fast_restore();
    if (!code) code=ov7725_preview_window_set(preview_sensor_zoom4x);
    if (code) {
        c3_camera_last_init_stage=2u;
        c3_camera_last_sensor_code=code;
        return 0u;
    }
    c3_camera_last_init_stage=0u;
    c3_camera_last_sensor_code=0u;
    return 1u; /* no FIFO rearm until caller restores AEC and AWB */
}

/* v180: waiting for sensor settling does not require FIFO writes. Count
 * N+1 fresh rising VSYNC edges: the first starts a new complete interval.
 * The caller has completed all sensor writes before entry. Ignore any
 * previous pending EXTI event; leave the ordinary final capture and its
 * pointer reset unchanged, including the post-settle SCCB gate. */
uint8_t c3_camera_wait_still_frames(unsigned frames, uint32_t timeout_ms)
{
    uint32_t ps = irq_lock();
    if (raw_frame_held || frame_sched.mode != C3_SCHED_STILL ||
        frame_sched.armed || frame_sched.writing || frame_sched.ready ||
        frame_sched.reading || frames > 6u || timeout_ms == 0u) {
        irq_unlock(ps);
        return 0u; /* never disturb a held or active frame */
    }
    GPIOB->BRR = GPIO_PIN_3;
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    uint32_t first = frame_sched.edges;
    irq_unlock(ps);
    if (frames == 0u) return 1u;
    uint32_t observed = first;
    uint32_t last_progress = HAL_GetTick();
    for (;;) {
        uint32_t count = frame_sched.edges;
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(count - first) >= frames + 1u) return 1u;
        if (count != observed) {
            observed = count;
            last_progress = now;
        } else if ((uint32_t)(now - last_progress) >= timeout_ms) {
            scheduler_stop();
            return 0u;
        }
        /* IRQs remain enabled; the scheduler stays idle and WEN stays off. */
        HAL_Delay(1u);
    }
}

/* Serial one-shot path, always used for RAW/DNG. Also available as the
 * C3_PREVIEW_PIPELINE=0 diagnostic fallback. Never overlap RAW read/write.
 */
uint8_t c3_camera_capture(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    if (frame_sched.mode != C3_SCHED_STILL) return 0u;
    write_pointer_reset();
    uint32_t ps = irq_lock();
    __HAL_GPIO_EXTI_CLEAR_IT(OV7725_VSYNC_Pin);
    c3_sched_arm_still(&frame_sched);
    irq_unlock(ps);
    while (frame_sched.ready == 0u) {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) {
            scheduler_stop();
            return 0u;
        }
    }
    return 1u;
}

/* Non-blocking FIFO producer. The scheduler allows two frames outstanding,
 * so a new capture can run while the previous frame is streamed to LCD.
 * Return 0 after timeout; upper layer reinitializes sensor and scheduler.
 */
uint8_t c3_camera_preview_acquire(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    for (;;) {
        uint32_t ps = irq_lock();
        uint8_t ok = c3_sched_acquire(&frame_sched);
        irq_unlock(ps);
        if (ok) {
            return 1u;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) {
            scheduler_stop();
            return 0u;
        }
    }
}

void c3_camera_preview_release(void)
{
    uint32_t ps = irq_lock();
    c3_sched_release(&frame_sched);
    irq_unlock(ps);
}

/* A RAW8 or RGB565 still is never re-armed after this point; VSYNC keeps interrupting,
 * but C3_SCHED_OFF prevents the ISR from enabling FIFO writes. */
uint8_t c3_camera_raw_hold(void)
{
    if (frame_sched.mode != C3_SCHED_STILL || frame_sched.ready == 0u ||
        frame_sched.writing) return 0u;
    scheduler_stop();
    OV7725_OE(1);
    raw_frame_held = 1u;
    return 1u;
}

void c3_camera_raw_discard(void)
{
    scheduler_stop();
    OV7725_OE(1);
    raw_frame_held = 0u;
}

/* AL422B data sheet 8.3.3: single write followed by multiple reads requires
 * resetting BOTH write and read addresses. The write reset is made with WEN
 * off to avoid replacing camera pixels, then the read pointer is reset.
 * Crucial bench caveat: on some FIFO timing interpretations /WRST while /WE
 * is inactive is deferred; validate full-frame CRC after two re-reads.
 * The sensor continues generating WCK for DRAM refresh while a still is held. */
uint8_t c3_camera_raw_rewind(void)
{
    if (!raw_frame_held || frame_sched.mode != C3_SCHED_OFF) return 0u;
    GPIOB->BRR = GPIO_PIN_3;
    write_pointer_reset();
    c3_camera_fifo_begin();
    return 1u;
}

void c3_camera_raw_read_end(void)
{
    OV7725_OE(1);
}

void c3_camera_fifo_begin(void)
{
    GPIOB->BRR = GPIO_PIN_3; /* stop capture for one-shot/RAW reads */
    OV7725_OE(0);
    __NOP(); __NOP();
    OV7725_RCLK(0);
    OV7725_RRST(0);
    __NOP(); __NOP();
    OV7725_RCLK(1);
    OV7725_RCLK(0);
    OV7725_RRST(1);
    OV7725_RCLK(1);
}

uint8_t c3_camera_fifo_read(void)
{
    return c3_camera_fifo_read_fast();
}

static inline __attribute__((always_inline)) uint16_t fifo_read565(void)
{
    uint16_t hi = c3_camera_fifo_read_fast();
    return (uint16_t)((hi << 8) | c3_camera_fifo_read_fast());
}

static inline __attribute__((always_inline)) uint16_t dim_rgb565(uint16_t c)
{
    return (uint16_t)((c & 0xF7DEu) >> 1);
}

static inline __attribute__((always_inline)) uint16_t grid_rgb565(uint16_t c)
{
    return (uint16_t)(dim_rgb565(c) + 0x7BEFu);
}

/* Same deliberately cheap RGB565 greyscale approximation as v0.1.1. */
static inline __attribute__((always_inline)) uint16_t grayscale565(uint16_t c)
{
    uint16_t r = (c >> 11) & 31u;
    uint16_t g = (c >> 5) & 63u;
    uint16_t b = c & 31u;
    uint16_t y5 = (uint16_t)((r * 3u + (g >> 1) * 6u + b) / 10u);
    if (y5 > 31u) y5 = 31u;
    return (uint16_t)((y5 << 11) | (y5 << 6) | y5);
}

#define C3_ROW_FIFO_BYTE() c3_camera_fifo_read_fast()
#define C3_ROW_DIM565(v) dim_rgb565(v)
#include "c3_preview_row_opt.h"
#undef C3_ROW_FIFO_BYTE
#undef C3_ROW_DIM565

/* v0.1.41: separately test overlay-specific CPU optimizations.
 * 0 = v0.1.40 O2 baseline; 1 = grid; 2 = grid + SRAM peaking;
 * 3 = grid + full-resolution staged peaking, field-tested D3 mainline.
 * These settings affect LIVE PREVIEW only. Still capture remains untouched.
 */
#include "c3_preview_effect_opt.h"
/* v0.1.57: optional centre-only peaking, default on. */

#include "c3_fifo_dma_live.inc"
/* Both QVGA and native VGA-centre-crop modes now yield 320x240
 * RGB565 directly from OV7725. A single FIFO->LCD renderer handles both,
 * including full-resolution peaking, grid, monochrome and OSD overlays. */
/* Complete 320x240 frame to LCD GRAM in one window transaction.
 * Overlay glyphs use a cached 1-bit mask in top/bottom bands: no more
 * per-glyph LCD windows or post-frame UI repaints.
 */
void c3_camera_preview_draw(const c3_settings_t *settings, uint8_t zoom4x,
                            uint8_t sd_ok,
                            uint32_t next_number, uint16_t fps_tenths,
                            uint8_t shoot_mode, uint8_t shutter_code,
                            uint8_t gain_step, uint8_t raw_dial_code,
                            int8_t ev_half_steps, uint8_t ev_active,
                            const c3_meter_t *meter)
{
    uint8_t grid = settings->grid;
    uint8_t monochrome = settings->monochrome;
    uint8_t ui_visible = settings->osd;
    uint8_t peaking = settings->peaking;
    uint8_t fps_enabled = settings->fps;
    uint8_t score_mode = c3_score_effective_mode(settings->focus_score);
    uint8_t score_enabled = (uint8_t)(score_mode != C3_SCORE_OFF);

    const uint8_t peak_area = c3_settings_peak_area(settings);
    c3_peak_exp_begin(&peak_exp,peak_area,peaking);
    uint8_t score_shared = (uint8_t)(peaking && c3_peak_exp_shared(peak_area,score_mode));
    c3_score_t *shared_sparse = (score_shared &&
        c3_peak_exp_shared_accum(peak_area,score_mode)
        ) ?
        &c3_score_state : (c3_score_t *)0;
    const uint8_t score_sample = c3_score_schedule_frame(&c3_score_state, score_mode, score_shared);
    uint16_t peak_color = c3_settings_color565(settings);
    uint8_t peak_threshold = c3_settings_peak_threshold(settings,c3_focus_state.threshold);
    uint32_t osd_now=HAL_GetTick();
    uint8_t card_red = c3_osd_update_card_warning(sd_ok, osd_now);
    c3_osd_update_exposure_warning(meter,shoot_mode,osd_now);
    card_red = (uint8_t)(card_red && ui_visible);
    const uint8_t shutter_auto=(uint8_t)(ui_visible && shutter_code==C3_EXPOSURE_AUTO_CODE);
    const uint8_t gain_auto=(uint8_t)(ui_visible && gain_step==0u);
    if (ui_visible) c3_osd_prepare(next_number, sd_ok, grid, monochrome,
                                   peaking, score_enabled, c3_score_state.score,
                                   fps_enabled, fps_tenths, shoot_mode,
                                   shutter_code, gain_step, raw_dial_code,
                                   ev_half_steps, ev_active, zoom4x,
                                   settings->output_format, meter);
    /* The first read resets RRST. Thereafter the read pointer is continuous
     * across frames, including AL422B's 393216-byte automatic wrap. */
    if (!preview_read_pointer_initialized) {
        /* Only the read-side reset is performed. Keep WEN controlled by the
         * VSYNC ISR: the next video frame may already be writing in parallel. */
        OV7725_OE(0);
        __NOP(); __NOP();
        OV7725_RCLK(0);
        OV7725_RRST(0);
        __NOP(); __NOP();
        OV7725_RCLK(1);
        OV7725_RCLK(0);
        OV7725_RRST(1);
        OV7725_RCLK(1);
        preview_read_pointer_initialized = 1u;
    } else {
        OV7725_OE(0); /* only gate outputs; do NOT reset read pointer */
    }
    c3_lcd_window(0, 0, C3_LCD_W - 1u, C3_LCD_HEIGHT - 1u);

    /* CPU retains the verified RCLK->GPIO sample path. Process one row into
     * 8-bit high/low RGB565 bytes while DMA2 sends the previous row to FSMC.
     * All overlays produce exactly the same output pixels as the CPU path. */
    if (!live_disabled) {
        const c3_live_render_t p={grid,monochrome,peaking,score_sample,
            score_shared,score_mode,peak_area,peak_threshold,peak_color};
        if(peaking)c3_focus_begin_frame(&c3_focus_state);
        uint8_t status=live_draw(&p,ui_visible,card_red,shutter_auto,gain_auto);
        if(status) {
            /* The read address may no longer be valid: stop and rearm both
             * sides instead of consuming the remaining frame with GPIO. */
            live_disabled=1u;
            c3_live_last_error=status; ++c3_live_errors;
            OV7725_OE(1);
            c3_focus_reset(&c3_focus_state); c3_score_reset(&c3_score_state);
            c3_camera_preview_rearm();
            live_error_hud();
            return;
        }
    } else {
    c3_lcd_dma_begin();
    if (peaking) c3_focus_begin_frame(&c3_focus_state);
    for (uint16_t y = 0u; y < C3_LCD_HEIGHT; ++y) {
        
        uint8_t *line = c3_lcd_dma_line(y);
        uint8_t osd_row = (uint8_t)(ui_visible &&
            (y < C3_OSD_BAND_H || y >= C3_LCD_HEIGHT - C3_OSD_BAND_H));
        const uint8_t *mask = osd_row ? c3_osd_row(y) : (const uint8_t *)0;
        uint8_t grid_y = (uint8_t)(grid && (y == 80u || y == 160u));
        if (peaking) {
            /* Peaking stays live on every displayed frame. Only rows needed
             * for gradient history, highlights or an independent score enter
             * the raw-row path. Other rows use the original FIFO/OSD path. */
            uint8_t peak_row=c3_peak_exp_row_active(peak_area,y,peak_exp.phase);
            uint8_t score_row=(uint8_t)(score_sample && !score_shared &&
                                      c3_score_row_active(score_mode,y));
            if (!peak_row && !score_row && !monochrome) {
                if (osd_row) {
                    c3_row_copy_osd(line,mask);
                    
                    if (grid) c3_peak_fast_dimmed_grid(line,mask);
                } else {
                    c3_row_copy_plain(line);
                    
                    if (grid) c3_effect_grid_row(line,y,0);
                }

            } else
            {
                c3_row_copy_plain(line);
                
                if (score_row) c3_score_sample_row(&c3_score_state,y,line);
                
                c3_peak_exp_pixels(line,y,peak_area,&c3_focus_state,peak_threshold,
                                    peak_color,monochrome,shared_sparse,&peak_exp);
                
                c3_peak_fast_overlay(line,y,grid,mask);
                
            }
        } else if (score_sample && c3_score_row_active(score_mode,y)) {
            c3_row_copy_plain(line);
            c3_score_sample_row(&c3_score_state,y,line);
            c3_effect_score_line(line,y,monochrome,grid,mask);
        } else
        if (grid && !monochrome) {
            /* Grid covers only 2 rows and 2 columns. Keep all other pixels
             * on the RowOpt2 fast path, patch the marked pixels in SRAM.
             * On OSD rows glyphs must win over grid (same as original). */
            if (osd_row) c3_row_copy_osd(line, mask);
            else c3_row_copy_plain(line);
            c3_effect_grid_row(line, y, osd_row ? mask : (const uint8_t *)0);
        } else
        if (!osd_row && !grid && !monochrome) {
            /* Optimized normal view: no 16-bit composition at all. */
            c3_row_copy_plain(line);
        } else if (osd_row && !grid && !monochrome) {
            c3_row_copy_osd(line, mask);
        } else {
            for (uint16_t x = 0u; x < C3_LCD_W; ++x) {
                uint16_t raw = fifo_read565();
                uint16_t color;
                if (osd_row) {
                    color = monochrome ? grayscale565(raw) : raw;
                    color = dim_rgb565(color);
                    if (grid && (x == 106u || x == 213u))
                        color = grid_rgb565(color);
                    if (mask[x >> 3] & (0x80u >> (x & 7u)))
                        color = 0xFFFFu;
                } else if (grid && !monochrome) {
                    color = (grid_y || x == 106u || x == 213u) ?
                            grid_rgb565(raw) : raw;
                } else {
                    color = monochrome ? grayscale565(raw) : raw;
                    if (grid && (grid_y || x == 106u || x == 213u))
                        color = grid_rgb565(color);
                }
                c3_lcd_dma_put565(line, x, color);
            }
        }
if (card_red && y >= C3_OSD_CARD_Y0 && y < C3_OSD_CARD_Y1)
            c3_osd_color_card_row(line, mask);
        if ((shutter_auto || gain_auto) && y >= C3_OSD_AUTO_Y0 && y < C3_OSD_AUTO_Y1)
            c3_osd_color_auto_row(line, mask, shutter_auto, gain_auto);
        
        c3_lcd_dma_submit(y);
        
    }
    {  c3_lcd_dma_finish();  }
    }
    if (peaking) c3_focus_end_frame_area(&c3_focus_state,
            c3_peak_exp_roi_count(peak_area)
        );

    if (score_sample) c3_score_end_frame(&c3_score_state);
    OV7725_OE(1);
    if(live_disabled)live_error_hud();
}

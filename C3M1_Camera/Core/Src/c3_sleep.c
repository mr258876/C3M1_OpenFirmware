#include "c3_sleep.h"
#define C3_SLEEP_PWM_PERIOD_US 4000u
#define C3_SLEEP_BREATH_PERIOD_MS 5000u

uint8_t c3_sd_led_should_light(uint8_t write_in_progress, uint8_t recent_write,
                               uint32_t now_ms, uint32_t last_write_ms)
{
    /* Unsigned subtraction also handles the SysTick 32-bit wraparound. */
    return (uint8_t)(write_in_progress ||
        (recent_write && (uint32_t)(now_ms-last_write_ms)<C3_SD_LED_HOLD_MS));
}
uint32_t c3_sleep_timeout_ms(uint8_t timer_choice)
{
    static const uint8_t seconds[5] = {0u,15u,30u,60u,180u};
    return timer_choice < 5u ? (uint32_t)seconds[timer_choice]*1000u : 0u;
}
uint8_t c3_sleep_due(uint8_t timer_choice,uint32_t now_ms,
                     uint32_t last_activity_ms,uint16_t pressed)
{
    uint32_t timeout=c3_sleep_timeout_ms(timer_choice);
    return (uint8_t)(timeout && !pressed &&
                    (uint32_t)(now_ms-last_activity_ms)>=timeout);
}
uint16_t c3_sleep_breath_us(uint32_t elapsed_ms)
{
    /* Raised-cosine-like easing, gamma-square for a natural human breathing
     * rhythm. Fixed point only: no floating point or costly trig on M3.
     * 5 s period, fade in/out mirrored around 2.5 s, max 75% duty. */
    uint32_t t=elapsed_ms % C3_SLEEP_BREATH_PERIOD_MS;
    if (t > 2500u) t = 5000u - t;
    uint32_t x=(t*256u)/2500u; /* 0..256 */
    uint32_t smooth=(x*x*(768u-2u*x)+32768u)>>16u; /* smoothstep, 0..256 */
    uint32_t brightness=(smooth*smooth+128u)>>8u;
    if (brightness>255u) brightness=255u;
    return (uint16_t)((brightness*3000u+127u)/255u);
}
#ifndef C3_SLEEP_HOST_TEST
#include "stm32f1xx_hal.h"
#include "stm32f1xx.h"
/* PD9 is not a TIM2 PWM output. TIM2 generates interrupts at 250Hz;
 * the IRQ toggles the already configured open-drain pin without remapping
 * any alternate-function pins that the camera/FIFO need. */
static volatile uint16_t sleep_on_us;
static volatile uint8_t led_active;
/* Disk writes are synchronous in the current FatFs SD_write path.
 * An LED grace period is kept in software, serviced only by the main loop. */
static uint8_t sd_write_in_progress;
static uint8_t sd_write_recent;
static uint32_t sd_last_done_ms;
#define C3_LED_ON()  (GPIOD->BRR = GPIO_PIN_9)
#define C3_LED_OFF() (GPIOD->BSRR = GPIO_PIN_9)
void c3_sleep_led_start(void)
{
    /* Sleep owns the LED exclusively; stale disk-write afterglow is cleared. */
    sd_write_in_progress=0u;
    sd_write_recent=0u;
    C3_LED_OFF();
    sleep_on_us=0u;
    led_active=1u;
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1=0u;
    TIM2->DIER=0u;
    /* 72 MHz TIM2 clock: APB1=36 MHz with x2 timer multiplier. */
    TIM2->PSC=71u;
    TIM2->ARR=C3_SLEEP_PWM_PERIOD_US-1u;
    TIM2->CCR1=1u;
    TIM2->CNT=0u;
    TIM2->EGR=TIM_EGR_UG;
    TIM2->SR=0u;
    TIM2->DIER=TIM_DIER_UIE | TIM_DIER_CC1IE;
    HAL_NVIC_SetPriority(TIM2_IRQn,6u,0u);
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1=TIM_CR1_ARPE | TIM_CR1_CEN;
}
void c3_sleep_led_update(uint32_t elapsed_ms)
{
    sleep_on_us=c3_sleep_breath_us(elapsed_ms);
}
void c3_sleep_led_irq(void)
{
    uint32_t flags=TIM2->SR & (TIM_SR_UIF | TIM_SR_CC1IF);
    /* TIM2 status flags are cleared by writing zero to their positions. */
    TIM2->SR=~flags;
    if (!led_active) return;
    if (flags & TIM_SR_UIF) {
        uint16_t duty=sleep_on_us;
        if (duty) {
            C3_LED_ON();
            TIM2->CCR1=duty;
        } else {
            C3_LED_OFF();
            TIM2->CCR1=1u;
        }
    }
    if (flags & TIM_SR_CC1IF) C3_LED_OFF();
}
void c3_sleep_led_stop(void)
{
    led_active=0u;
    TIM2->DIER=0u;
    TIM2->CR1=0u;
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    TIM2->SR=0u;
    __HAL_RCC_TIM2_CLK_DISABLE();
    sleep_on_us=0u;
    sd_write_in_progress=0u;
    sd_write_recent=0u;
    C3_LED_OFF(); /* awake idle is DARK; only real SD writes light it */
}
void c3_sd_led_write_begin(void)
{
    if (led_active) return; /* sleeping never performs SD transactions */
    sd_write_in_progress=1u;
    C3_LED_ON();
}
void c3_sd_led_write_end(void)
{
    if (led_active) return;
    sd_last_done_ms=HAL_GetTick();
    sd_write_recent=1u;
    sd_write_in_progress=0u;
    /* Keep the LED lit until foreground polling expires the grace period. */
}
void c3_sd_led_poll(uint32_t now_ms)
{
    if (led_active) return; /* do not interfere with TIM2 breathing PWM */
    if (c3_sd_led_should_light(sd_write_in_progress,sd_write_recent,
                               now_ms,sd_last_done_ms)) {
        C3_LED_ON();
    } else {
        sd_write_recent=0u;
        C3_LED_OFF();
    }
}
#endif

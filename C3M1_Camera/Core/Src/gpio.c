/* C3M1 production PCB GPIO map; do not regenerate this file from the
 * original ALIENTEK/STM32CubeMX .ioc (that .ioc is for a different board).
 * Pin names verified against the supplied C3M1 main and top board schematics.
 */
#include "gpio.h"
#include "c3_board_pins.h"

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PB3/PB4 are JTAG pins by default. Leave PA13/PA14 available for SWD. */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* PRODUCTION SCHEMATIC: PB3=WEN, PB4=RCLK, PG15=/OE. PB5 NC!
     * WEN=0 disables writes, RCLK=0, /OE=1 tristates the FIFO output. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_3 | GPIO_PIN_4;
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    /* OV7725 SCCB SCL=PD3: the CMOS daughterboard has a pull-up to DOVDD
     * (~2.8 V). Release HIGH with open-drain, do NOT drive 3.3 V push-pull.
     * The successfully run diagnostic firmware uses this exact mode.
     * PD10 is on /WE and must remain Hi-Z (driven by U7). */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3 | GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);
    /* PD9 is active-low: keep the status LED dark from GPIO bring-up.
     * Only SD sector writes (or the sleep-breathing timer) may illuminate it. */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_6 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOD, &g);
    /* Red status LED on PD9 sinks current from VCC5 through R2=1k.
     * Open-drain avoids ever driving 3.3 V against that LED supply. */
    g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = GPIO_PIN_10; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &g);

    /* Camera SCCB data (open drain), FIFO read reset and output enable. */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_14 | GPIO_PIN_15; g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &g);
    g.Pin = GPIO_PIN_13; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOG, &g);
    /* D0..D7 wired to PG1..PG8 (one-bit offset from port bit zero). */
    g.Pin = 0x01FEu; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOG, &g);

    /* Real board: PF4=LCDEN (LOW=off), PF5=LCDRST (LOW=assert).
     * IMPORTANT: PF3 is KEY4X, not a push-pull output! */
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN | C3_LCD_RESET_PIN, GPIO_PIN_RESET);
    g.Pin = C3_LCD_EN_PIN | C3_LCD_RESET_PIN; g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOF, &g);

    /* ISO=PF1, shutter=PF2, 4X=PF3, film=PF10. PF0 is NC. */
    g.Pin = C3_BTN_MASK;
    g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOF, &g);

    /* TSR-16: R10 pulls up the COMMON contact, not TSR1/PF6.
       Four separate contacts TSR1/2/4/8 -> PF6/7/8/9. Open contact = 0
       with MCU pull-down; closed contact = 1 via common pull-up.
       Never pull PF6 up permanently: code 00 must remain 0000. */
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOF, &g);

    /* Battery divider: PA1 ADC1_IN1. */
    g.Pin = GPIO_PIN_1; g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    /* VSYNC: capture frame boundaries with EXTI8 on PA8. */
    g.Pin = GPIO_PIN_8; g.Mode = GPIO_MODE_IT_RISING; g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

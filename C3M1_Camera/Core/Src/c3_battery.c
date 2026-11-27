#include "c3_battery.h"

uint16_t c3_battery_raw_to_mv(uint16_t raw12)
{
    /* 12-bit full scale 4095 = assumed 3.3V x R17+R18 / R18 = 6.6V.
     * Use 32-bit arithmetic, rounding to the nearest millivolt. */
    if (raw12 > 4095u) raw12 = 4095u;
    return (uint16_t)(((uint32_t)raw12 * 6600u + 2047u) / 4095u);
}

uint8_t c3_battery_estimate_percent(uint16_t mv)
{
    /* Approximate SINGLE-CELL Li-ion open-circuit curve. Only advisory.
     * Voltage under load / while charging varies significantly. */
    static const struct { uint16_t mv; uint8_t pct; } knots[] = {
        {3200u, 0u}, {3300u, 1u}, {3400u, 3u}, {3500u, 8u},
        {3600u, 15u}, {3700u, 30u}, {3800u, 50u}, {3900u, 65u},
        {4000u, 78u}, {4100u, 90u}, {4200u, 100u}
    };
    if (mv <= knots[0].mv) return 0u;
    for (unsigned i=1u; i<sizeof(knots)/sizeof(knots[0]); ++i) {
        if (mv < knots[i].mv) {
            const uint32_t span=(uint32_t)(knots[i].mv-knots[i-1u].mv);
            const uint32_t delta=(uint32_t)(mv-knots[i-1u].mv);
            const uint32_t rise=(uint32_t)(knots[i].pct-knots[i-1u].pct);
            return (uint8_t)(knots[i-1u].pct+(delta*rise+span/2u)/span);
        }
    }
    return 100u;
}

void c3_battery_format_unknown(char out[16])
{
    if (!out) return;
    const char s[]="ADC ERR";
    unsigned i=0u;
    for (;i<sizeof(s);++i) out[i]=s[i];
}

void c3_battery_format(char out[16], uint16_t mv)
{
    if (!out) return;
    if (mv < 2500u) {
        const char s[]="BAT LOW";
        for (unsigned i=0u;i<sizeof(s);++i) out[i]=s[i];
        return;
    }
    /* Typical battery voltage fits 4 ASCII digits (X.XX V). */
    if (mv > 9990u) mv = 9990u;
    const uint16_t centivolts=(uint16_t)((mv+5u)/10u);
    uint8_t p=c3_battery_estimate_percent(mv);
    unsigned i=0u;
    out[i++]=(char)('0'+centivolts/100u);
    out[i++]='.';
    out[i++]=(char)('0'+(centivolts/10u)%10u);
    out[i++]=(char)('0'+centivolts%10u);
    out[i++]='V'; out[i++]=' '; out[i++]='~';
    if (p>=100u) out[i++]='1';
    if (p>=10u) out[i++]=(char)('0'+(p/10u)%10u);
    out[i++]=(char)('0'+p%10u);
    out[i++]='%'; out[i]=0;
}

#include "stm32f1xx_hal.h"

/* Lazy ADC1 init: the first read can occur after the first preview frame.
 * No ADC operation is performed inside the FIFO-to-LCD pixel loop.
 * This is the ONLY module owning ADC1; PA1 is configured analog by gpio.c. */
static uint8_t adc_ready;
static uint8_t adc_wait_clear(volatile uint32_t *reg,uint32_t mask)
{
    for (uint32_t timeout=100000u;timeout;--timeout)
        if ((*reg & mask)==0u) return 1u;
    return 0u;
}
static uint8_t adc_setup(void)
{
    if (adc_ready) return 1u;
    __HAL_RCC_ADC1_CLK_ENABLE();
    /* PCLK2=72 MHz. F103 ADC max frequency is 14 MHz; /6 = 12 MHz. */
    RCC->CFGR=(RCC->CFGR & ~RCC_CFGR_ADCPRE)|RCC_CFGR_ADCPRE_DIV6;
    ADC1->CR1=0u;
    ADC1->CR2=0u;
    ADC1->SQR1=0u; /* one regular conversion */
    ADC1->SQR2=0u;
    ADC1->SQR3=1u; /* ADC1_IN1 on PA1 */
    ADC1->SMPR2=(ADC1->SMPR2 & ~(7u<<3u)) | (7u<<3u); /* 239.5 cycles */
    ADC1->CR2=ADC_CR2_ADON;
    HAL_Delay(1u); /* One-time ADC startup stabilization, outside the pixel loop. */
    ADC1->CR2|=ADC_CR2_RSTCAL;
    if (!adc_wait_clear(&ADC1->CR2,ADC_CR2_RSTCAL)) return 0u;
    ADC1->CR2|=ADC_CR2_CAL;
    if (!adc_wait_clear(&ADC1->CR2,ADC_CR2_CAL)) return 0u;
    ADC1->CR2|=ADC_CR2_EXTSEL|ADC_CR2_EXTTRIG; /* SWSTART trigger */
    adc_ready=1u;
    return 1u;
}
static uint8_t adc_once(uint16_t *out)
{
    (void)ADC1->DR; /* discard any previous EOC, then launch a new sample */
    ADC1->CR2|=ADC_CR2_SWSTART;
    uint32_t timeout=100000u;
    while ((ADC1->SR&ADC_SR_EOC)==0u && --timeout) { }
    if (!timeout) return 0u;
    *out=(uint16_t)(ADC1->DR & 0x0FFFu);
    return 1u;
}
uint8_t c3_battery_read_mv(uint16_t *mv)
{
    if (!mv || !adc_setup()) return 0u;
    uint16_t sample;
    if (!adc_once(&sample)) return 0u; /* settle the input mux */
    uint32_t total=0u;
    for (unsigned i=0u;i<8u;i++) {
        if (!adc_once(&sample)) return 0u;
        total+=sample;
    }
    *mv=c3_battery_raw_to_mv((uint16_t)((total+4u)/8u));
    return 1u;
}

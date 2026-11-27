/* Host-execute actual production SCCB byte receiver with a simulated PG13.
 * Synthetic sensor drives one byte on each rising SCL edge.  The mock models
 * the observed OD release anomaly for bit7=1 (not a physical fault model).
 * Its purpose is to verify GPIO ownership, byte order, 8 clocks and restore.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define __SCCB_H
#define __DELAY_H
#define OV_SCCB_TYPE_NOD 0
#define GPIO_PIN_13 (1u << 13)
static struct { volatile uint32_t CRH, IDR, ODR; } gpiog;
#define GPIOG (&gpiog)
static uint8_t expected_byte;
static unsigned byte_bit, scl_rising, sda_sample_count, float_samples, drive_low_writes;
static uint32_t baseline_crh;
static void fake_scl_high(void)
{
    ++scl_rising;
    if (byte_bit < 8u) {
        const uint32_t nibble = (gpiog.CRH >> 20) & 15u;
        const unsigned bit = (expected_byte >> (7u - byte_bit)) & 1u;
        unsigned sda = bit;
        /* Model the real failure case: SDA not decoded in OD mode for bit7=1. */
        if (nibble != 4u && (expected_byte & 0x80u)) sda = (byte_bit == 0u) ? 0u : 1u;
        if (sda) gpiog.IDR |= GPIO_PIN_13;
        else gpiog.IDR &= ~GPIO_PIN_13;
        ++byte_bit;
    }
}
static void fake_scl_low(void) {}
static uint8_t fake_sda_read(void)
{
    ++sda_sample_count;
    if (((gpiog.CRH >> 20) & 15u) == 4u) ++float_samples;
    return (gpiog.IDR & GPIO_PIN_13) ? 1u : 0u;
}
static void fake_sda_write(unsigned v)
{
    if (!v) ++drive_low_writes;
    if (v) gpiog.ODR |= GPIO_PIN_13;
    else gpiog.ODR &= ~GPIO_PIN_13;
}
#define SCCB_SCL(v) do { if (v) fake_scl_high(); else fake_scl_low(); } while (0)
#define SCCB_SDA(v) fake_sda_write((v))
#define SCCB_READ_SDA fake_sda_read()
static void delay_us(uint32_t usec) { (void)usec; }
typedef struct { uint8_t low, early, late; } c3_sccb_trace_t;
void sccb_stop(void);
#include "../C3M1_Camera/Drivers/BSP/OV7725/sccb.c"

static void test_byte(uint8_t value)
{
    expected_byte=value;
    baseline_crh = (0xABCD4321u & ~(15u << 20)) | (6u << 20);
    gpiog.CRH = baseline_crh;
    gpiog.ODR = GPIO_PIN_13;
    gpiog.IDR = GPIO_PIN_13;
    byte_bit=scl_rising=sda_sample_count=float_samples=drive_low_writes=0u;
    uint8_t got=sccb_read_byte();
    assert(byte_bit == 8u && scl_rising == 8u);
    assert(sda_sample_count == 8u);
    assert(drive_low_writes == 0u);   /* no active SDA drive during RX byte */
    assert(gpiog.CRH == baseline_crh); /* restore PG13, leave PG14/15 untouched */
    assert(gpiog.ODR & GPIO_PIN_13);  /* safe to restore OD: ODR remains 1 */
#if C3_SCCB_FLOAT_READ
    assert(float_samples == 8u);
    assert(got == value);
#else
    assert(float_samples == 0u);
    assert(got == ((value & 0x80u) ? 0x7fu : value));
#endif
}
int main(void)
{
    static const uint8_t cases[] = { 0xA2u,0x93u,0xB0u,0x13u,0x30u,0x00u,0xFFu,0x7Fu,0x80u };
    for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) test_byte(cases[i]);
    puts("PASS: SCCB RX bit order, PG13 floating ownership, 8 clocks, safe OD restore");
    return 0;
}

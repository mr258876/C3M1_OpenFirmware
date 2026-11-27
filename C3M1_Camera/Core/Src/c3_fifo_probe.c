/* C3M1 FIFO probe v0.1.24: corrected GPIO map after schematic recheck.
 * Never mounts the SD or touches settings Flash.  Two capture strategies:
 *  OLD: /WRST pulse while /WE inactive (legacy firmware behaviour).
 *  PRIME: hold /WRST low through one full enabled producer frame,
 *         release /WRST at VSYNC, then capture a fresh frame at address 0.
 *  This avoids assuming that write-reset works while /WE is disabled.
 * Also permits old pre-edge or datasheet post-rising-edge RCK sampling.
 * With OV7725 COM3[0]=1, stable color bars are expected if capture works.
 */
#include "c3_fifo_probe.h"
#include "c3_camera.h"
#include "c3_lcd.h"
#include "c3_input.h"
#include "c3_board_pins.h"
#include "main.h"
#include "../../Drivers/BSP/OV7725/ov7725.h"
#define COM3 0x0Cu /* OV7725 color-bar test pattern register */

#define SHOT_BIT C3_BTN_SHUTTER
#define ISO_BIT C3_BTN_ISO
#define ZOOM_BIT C3_BTN_4X
#define FILM_BIT C3_BTN_FILM
#define FRAME_BYTES (320u * 240u * 2u)

volatile uint32_t c3_fp_frame;
volatile uint32_t c3_fp_unique;
volatile uint32_t c3_fp_zero_bytes;
volatile uint32_t c3_fp_ff_bytes;
volatile uint32_t c3_fp_first4;
volatile uint32_t c3_fp_vsync;
volatile uint32_t c3_fp_error;
volatile uint32_t c3_fp_com3;
volatile uint32_t c3_fp_mode;
/* SWD values and on-screen diagnosis for PG1..PG8. */
volatile uint32_t c3_fp_off_pd, c3_fp_off_pu;
volatile uint32_t c3_fp_on_pd, c3_fp_on_pu;
volatile uint32_t c3_fp_on_unique;
/* bit0 saw LOW, bit1 saw HIGH at PD10 (/WE). */
volatile uint32_t c3_fp_we_disabled, c3_fp_we_enabled;
volatile uint32_t c3_fp_gate_enable_odr;


static void gpio_write_enable(uint8_t on)
{
    if (on) GPIOB->BSRR = GPIO_PIN_3;
    else GPIOB->BRR = GPIO_PIN_3;
}
static void gpio_write_reset(uint8_t release)
{
    if (release) GPIOD->BSRR = GPIO_PIN_6;
    else GPIOD->BRR = GPIO_PIN_6;
}
static uint8_t next_edge(uint32_t ms)
{
    uint32_t start_tick = HAL_GetTick();
    uint32_t seen = c3_camera_vsync_edges;
    while (c3_camera_vsync_edges == seen) {
        if ((uint32_t)(HAL_GetTick() - start_tick) > ms) return 0u;
    }
    return 1u;
}
static uint8_t capture_frame(uint8_t primed)
{
    /* c3_camera_preview_stop() leaves the VSYNC ISR running, but forces its
       frame scheduler OFF, and turns /WE off at every VSYNC rising edge. */
    c3_camera_preview_stop();
    gpio_write_enable(0);
    gpio_write_reset(1);
    HAL_Delay(3u);

    if (primed) {
        /* A FULL frame with /WRST asserted while /WE is active is a deliberate
           throwaway. This guarantees at least one /WRST sample on an ENABLED
           WCK cycle, unlike the legacy disabled-write pulse. */
        gpio_write_reset(0);
        if (!next_edge(1500u)) goto timeout;
        gpio_write_enable(1);
        if (!next_edge(1500u)) goto timeout;  /* ISR stops write */
        gpio_write_enable(0);
        gpio_write_reset(1);
    } else {
        /* Legacy behaviour for A/B diagnosis only: may not reset /WRST. */
        gpio_write_reset(0);
        HAL_Delay(1u);
        gpio_write_reset(1);
    }
    /* Capture one full next field.  WAIT for both start and end VSYNC. */
    if (!next_edge(1500u)) goto timeout;
    gpio_write_enable(1);
    if (!next_edge(1500u)) goto timeout;
    gpio_write_enable(0);
    return 1u;
timeout:
    gpio_write_enable(0);
    gpio_write_reset(1);
    return 0u;
}
static void read_begin(uint8_t rising_sample)
{
    gpio_write_enable(0);
    GPIOG->BRR = GPIO_PIN_15;    /* /OE low: FIFO drives PG1..PG8 */
    for (volatile unsigned i=0;i<32u;i++) __NOP();
    if (!rising_sample) {
        c3_camera_fifo_begin(); /* v0.1.21 legacy reset sequence */
        return;
    }
    GPIOB->BRR = GPIO_PIN_4;     /* RCK low */
    GPIOG->BRR = GPIO_PIN_14;    /* /RRST low */
    for (unsigned k=0;k<16u;k++) __NOP();
    GPIOB->BSRR = GPIO_PIN_4;    /* clock reset on RCK rising */
    for (unsigned k=0;k<16u;k++) __NOP();
    GPIOB->BRR = GPIO_PIN_4;
    GPIOG->BSRR = GPIO_PIN_14;   /* /RRST released before first read */
    for (unsigned k=0;k<16u;k++) __NOP();
}
static inline __attribute__((always_inline)) uint8_t read_byte_rise(void)
{
    GPIOB->BSRR = GPIO_PIN_4;    /* actual data updates after RCK rising */
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    uint8_t v = (uint8_t)((GPIOG->IDR >> 1) & 0xffu);
    GPIOB->BRR = GPIO_PIN_4;
    __NOP(); __NOP(); __NOP(); __NOP();
    return v;
}
static void render_fifo(uint8_t rising_sample)
{
    uint8_t bitset[32] = {0u};
    uint32_t n0=0u,nff=0u, first4=0u;
    read_begin(rising_sample);
    c3_lcd_window(0,0,319,239);
    for (uint32_t i=0; i<FRAME_BYTES; ++i) {
        uint8_t b = rising_sample ? read_byte_rise() : c3_camera_fifo_read_fast();
        if (i < 4u) first4 = (first4 << 8) | b;
        bitset[b >> 3] |= (uint8_t)(1u << (b & 7u));
        n0 += b==0u;
        nff += b==0xffu;
        c3_lcd_write_byte_fast(b);
    }
    GPIOG->BSRR = GPIO_PIN_15; /* /OE off */
    unsigned unique=0;
    for (unsigned i=0;i<32u;++i) {
        uint8_t b=bitset[i];
        while (b) { unique += b&1u; b>>=1; }
    }
    c3_fp_first4=first4;
    c3_fp_zero_bytes=n0;
    c3_fp_ff_bytes=nff;
    c3_fp_unique=unique;
    c3_fp_vsync=c3_camera_vsync_edges;
    ++c3_fp_frame;
}
/* AL422B output bus tri-state diagnosis. All pins PG1..PG8 are inputs.
 * Pull changes are made ONLY with /OE disabled to avoid driving against
 * AL422B. Then read 64 RCK-clocked samples with /OE enabled.
 * When AL422B is properly driving, changing input pulls MUST NOT change
 * the 8-bit data. An un-driven bus will follow pull-up/down instead.
 */
static uint8_t bus_sample(uint8_t oe_on, uint8_t pull_up, uint8_t *unique)
{
    GPIOG->BSRR = GPIO_PIN_15; /* /OE HIGH => external outputs high-Z */
    GPIO_InitTypeDef g = {0};
    g.Pin = 0x01FEu;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = pull_up ? GPIO_PULLUP : GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOG, &g);
    for (volatile unsigned i = 0; i < 64u; ++i) __NOP();
    if (oe_on) GPIOG->BRR = GPIO_PIN_15; /* enable FIFO output drive */
    for (volatile unsigned i = 0; i < 64u; ++i) __NOP();
    uint8_t first = 0;
    uint8_t set[32] = {0};
    for (unsigned k = 0; k < 64u; ++k) {
        GPIOB->BRR = GPIO_PIN_4;
        for (unsigned n=0; n<8u; ++n) __NOP();
        GPIOB->BSRR = GPIO_PIN_4;
        for (unsigned n=0; n<16u; ++n) __NOP();
        uint8_t value = (uint8_t)((GPIOG->IDR >> 1) & 0xffu);
        if (k == 0u) first = value;
        set[value >> 3] |= (uint8_t)(1u << (value & 7u));
    }
    GPIOG->BSRR = GPIO_PIN_15; /* /OE HIGH */
    unsigned count = 0u;
    for (unsigned i=0;i<32u;++i)
        for (unsigned b=0;b<8u;++b) count += (set[i] >> b) & 1u;
    *unique = (uint8_t)count; /* 64 samples max */
    return first;
}
static void bus_probe(void)
{
    uint8_t unique = 0u;
    /* FIFO write stays disabled throughout this GPIO input experiment. */
    gpio_write_enable(0u);
    c3_fp_off_pd = bus_sample(0u,0u,&unique);
    c3_fp_off_pu = bus_sample(0u,1u,&unique);
    c3_fp_on_pd = bus_sample(1u,0u,&unique);
    c3_fp_on_unique = unique;
    c3_fp_on_pu = bus_sample(1u,1u,&unique);
    /* Leave the bus as floating input for the following normal frame. */
    GPIO_InitTypeDef g = {0};
    g.Pin = 0x01FEu; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOG,&g);
    GPIOG->BSRR = GPIO_PIN_15;
}
/* Sample the physically connected /WE net at PD10 during a few sensor
 * frame periods. U4 is a NAND of WEN and HREF, U7 is an active-low buffer
 * enabled when PD11=0. /WE is active LOW. We need to see BOTH levels when
 * WEN=1; with WEN=0, it should remain HIGH. No SD or photos are involved.
 */
static uint8_t sample_write_gate(uint8_t enable, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t levels = 0u;
    gpio_write_enable(enable);
    while ((uint32_t)(HAL_GetTick() - t0) < ms) {
        uint32_t d = GPIOD->IDR;
        levels |= (d & GPIO_PIN_10) ? 2u : 1u;
    }
    gpio_write_enable(0u);
    return levels;
}
static void write_gate_probe(void)
{
    c3_camera_preview_stop();
    /* Stop the VSYNC ISR from repeatedly forcing PB3 WEN OFF during
     * this bounded write-gate test. SysTick remains enabled. */
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
    c3_fp_gate_enable_odr = (GPIOD->ODR & GPIO_PIN_11) ? 1u : 0u;
    c3_fp_we_disabled = sample_write_gate(0u,40u);
    c3_fp_we_enabled = sample_write_gate(1u,220u);
    HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}
static char hd(uint8_t b) { b&=15u; return (char)(b<10u ? '0'+b : 'A'+b-10u); }
static void put_hex(char *s, unsigned start, uint32_t v, unsigned digits)
{
    for (unsigned i=0;i<digits;i++) {
        s[start+digits-1u-i]=hd((uint8_t)v);
        v>>=4;
    }
}
static void render_info(uint8_t prime,uint8_t rise,uint8_t colorbar,uint8_t freeze)
{
    c3_lcd_rect(0,156,320,84,0x0000u);
    char off[] = "OFF DN=00 UP=00";
    put_hex(off,7,c3_fp_off_pd,2);
    put_hex(off,13,c3_fp_off_pu,2);
    c3_lcd_text(3,158,off,0x07FFu,1);
    char on[] = "ON  DN=00 UP=00 UC=00";
    put_hex(on,7,c3_fp_on_pd,2);
    put_hex(on,13,c3_fp_on_pu,2);
    put_hex(on,19,c3_fp_on_unique,2);
    c3_lcd_text(3,169,on,0x07FFu,1);
    char we[] = "WE0=0 WE1=0 GE=0";
    put_hex(we,4,c3_fp_we_disabled,1);
    put_hex(we,10,c3_fp_we_enabled,1);
    put_hex(we,15,c3_fp_gate_enable_odr,1);
    c3_lcd_text(3,180,we,0xFFE0u,1);
    c3_lcd_text(3,191,prime ? "WRST PRIME" : "WRST OLD",0xFFE0u,1);
    c3_lcd_text(114,191,rise ? "RCK RISE" : "RCK OLD",0xFFE0u,1);
    c3_lcd_text(230,191,colorbar ? "BAR ON" : "BAR OFF",0xFFE0u,1);
    char row[]="U 000 Z 00000 F 00000";
    put_hex(row,2,c3_fp_unique,3);
    put_hex(row,8,c3_fp_zero_bytes,5);
    put_hex(row,16,c3_fp_ff_bytes,5);
    c3_lcd_text(3,201,row,0xffffu,1);
    char first[]="FIRST 00000000";
    put_hex(first,6,c3_fp_first4,8);
    c3_lcd_text(3,211,first,0xffffu,1);
    char row2[]="N 0000 VS 0000 C3 00";
    put_hex(row2,2,c3_fp_frame,4);
    put_hex(row2,10,c3_fp_vsync,4);
    put_hex(row2,18,c3_fp_com3,2);
    c3_lcd_text(3,221,row2,0x07E0u,1);
    if (freeze) c3_lcd_text(220,221,"PAUSE",0xFFE0u,1);
}
void c3_fifo_probe_run(uint8_t camera_ok)
{
    if (!camera_ok) {
        c3_lcd_message("SENSOR ERROR");
        for (;;) HAL_Delay(500);
    }
    /* Test-pattern from OV7725's COM3 bit 0, not a GUI fake. This removes
       scene motion/exposure from the pixel-path check. */
    uint8_t prime=1u, rise=1u, colorbar=1u, paused=0u;
    c3_fp_frame=0u; c3_fp_error=0u;
    c3_camera_preview_stop();
    write_gate_probe(); /* WEN off/on level sampling while camera runs */
    ov7725_write_reg(COM3,0x51u); /* base configuration COM3=0x50 */
    c3_fp_com3=ov7725_read_reg(COM3);
    HAL_Delay(200u);
    c3_input_reset((uint16_t)(~GPIOF->IDR)&(ISO_BIT|SHOT_BIT|ZOOM_BIT|FILM_BIT),HAL_GetTick());
    for (;;) {
        c3_input_batch_t b=c3_input_take();
        uint16_t taps=b.events.short_released;
        if (taps & ZOOM_BIT) prime^=1u;
        if (taps & FILM_BIT) rise^=1u;
        if (taps & ISO_BIT) {
            colorbar^=1u;
            ov7725_write_reg(COM3,colorbar?0x51u:0x50u);
            c3_fp_com3=ov7725_read_reg(COM3);
            HAL_Delay(150u);
        }
        if (b.events.pressed & SHOT_BIT) paused^=1u;
        c3_fp_mode=(uint32_t)(prime | rise<<1 | colorbar<<2 | paused<<3);
        if (!paused) {
            if (!capture_frame(prime)) {
                c3_fp_error=1u;
                c3_lcd_message("FIFO VSYNC TIMEOUT");
                HAL_Delay(400u);
                continue;
            }
            render_fifo(rise);
            bus_probe();
        }
        render_info(prime,rise,colorbar,paused);
        HAL_Delay(170u);
    }
}

/* ST7789V3, 8-bit 8080 / FSMC bank4, CD=PG0/FSMC_A10.
 * Use byte writes on both ports. A legacy 16-bit memory-mapped LCD struct
 * will NOT work correctly on the production C3M1 PCB.
 */
#include "c3_lcd.h"
#include "c3_font.h"
#include "c3_board_pins.h"
#include "main.h"

#define LCD_CMD (*(volatile uint8_t *)0x6C000000u)
#define LCD_DATA C3_LCD_DATA8
static inline void cmd(uint8_t x) { LCD_CMD = x; }
static inline void data(uint8_t x) { LCD_DATA = x; }
/* ST7789's MADCTL BGR and display inversion bits are panel-global.
 * Preserve both when the user's LCD rotation changes. */
static uint8_t lcd_bgr = C3_LCD_DEFAULT_BGR;
static uint8_t lcd_inversion_on = C3_LCD_DEFAULT_INV_ON;
static uint8_t lcd_rotate_180 = 1u;
static uint8_t lcd_madctl(void)
{
    return (uint8_t)((lcd_rotate_180 ? 0xA0u : 0x60u) |
                     (lcd_bgr ? 0x08u : 0u));
}
static void cmd_bytes(uint8_t op, const uint8_t *p, unsigned count)
{
    cmd(op); while (count--) data(*p++);
}
void c3_lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    cmd(0x2A); data(x0>>8); data(x0); data(x1>>8); data(x1);
    cmd(0x2B); data(y0>>8); data(y0); data(y1>>8); data(y1);
    cmd(0x2C);
}
void c3_lcd_pixel_stream(uint16_t c)
{
    c3_lcd_write565_fast(c);
}
void c3_lcd_init(void)
{
    /* Preserve init parameters from the manufacturer's included ST7789 BSP. */
    static const uint8_t porch[] = {0x0C,0x0C,0x00,0x33,0x33};
    static const uint8_t gamma_p[] =
        {0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17};
    static const uint8_t gamma_n[] =
        {0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E};
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN, GPIO_PIN_RESET); /* backlight off, PF4 */
    HAL_GPIO_WritePin(GPIOF, C3_LCD_RESET_PIN, GPIO_PIN_RESET); /* /RESET PF5 */
    HAL_Delay(20);
    HAL_GPIO_WritePin(GPIOF, C3_LCD_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(120);
    cmd(0x01); HAL_Delay(140); /* software reset */
    cmd(0x11); HAL_Delay(120); /* sleep out */
    cmd(0x36); data(lcd_madctl()); /* 180 deg, vendor ST7789 BGR bit */
    cmd(0x3A); data(0x55); /* 16-bit RGB565: each pixel transmitted as 2x8-bit */
    cmd_bytes(0xB2, porch, sizeof(porch));
    cmd(0xB7); data(0x35);
    cmd(0xBB); data(0x32);
    cmd(0xC0); data(0x0C);
    cmd(0xC2); data(0x01);
    cmd(0xC3); data(0x10);
    cmd(0xC4); data(0x20);
    cmd(0xC6); data(0x0F);
    { static const uint8_t power[] = {0xA4,0xA1};
      cmd_bytes(0xD0, power, sizeof(power)); }
    cmd_bytes(0xE0, gamma_p, sizeof(gamma_p));
    cmd_bytes(0xE1, gamma_n, sizeof(gamma_n));
    /* The C3M1 panel requires INVON (0x21) in normal use. Keep this
     * switchable so the read-only diagnostic can compare both modes. */
    cmd(lcd_inversion_on ? 0x21u : 0x20u);
    cmd(0x29); HAL_Delay(30);
    c3_lcd_fill(0x0000);
    /* Caller must finish orientation and first clean drawing before lighting. */
}
void c3_lcd_backlight_on(void)
{
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN, GPIO_PIN_SET);
}
void c3_lcd_backlight_off(void)
{
    HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN, GPIO_PIN_RESET);
}
void c3_lcd_enter_sleep(void)
{
    /* All LCD DMA / FIFO activity must be finished by caller. */
    c3_lcd_backlight_off();
    cmd(0x28u);              /* DISPOFF */
    HAL_Delay(20u);
    cmd(0x10u);              /* SLPIN */
    HAL_Delay(120u);        /* ST7789 sleep-in transition */
}
void c3_lcd_leave_sleep(void)
{
    /* Keep backlight off until an initialized, black GRAM is ready. */
    c3_lcd_backlight_off();
    cmd(0x11u);              /* SLPOUT */
    HAL_Delay(120u);
    c3_lcd_fill(0x0000u);
    cmd(0x29u);              /* DISPON */
    HAL_Delay(30u);
    c3_lcd_backlight_on();
}
void c3_lcd_set_rotation(uint8_t rotate_180)
{
    lcd_rotate_180 = rotate_180 ? 1u : 0u;
    cmd(0x36);
    data(lcd_madctl());
    /* A rotation changes GRAM address mapping; discard the old image. */
    c3_lcd_fill(0x0000u);
}
void c3_lcd_set_color_mode(uint8_t bgr, uint8_t inversion_on)
{
    lcd_bgr = bgr ? 1u : 0u;
    lcd_inversion_on = inversion_on ? 1u : 0u;
    cmd(0x36);
    data(lcd_madctl());
    cmd(lcd_inversion_on ? 0x21u : 0x20u);
}
void c3_lcd_fill(uint16_t c)
{
    c3_lcd_window(0,0,C3_LCD_W-1,C3_LCD_HEIGHT-1);
    for (uint32_t i=0;i<C3_LCD_W*C3_LCD_HEIGHT;i++) c3_lcd_write565_fast(c);
}
void c3_lcd_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x<0) {w+=x;x=0;} if (y<0) {h+=y;y=0;}
    if (x>=320 || y>=240 || w<=0 || h<=0) return;
    if (x+w>320) w=320-x; if (y+h>240) h=240-y;
    c3_lcd_window((uint16_t)x,(uint16_t)y,(uint16_t)(x+w-1),(uint16_t)(y+h-1));
    for (int i=0;i<w*h;i++) c3_lcd_write565_fast(c);
}
/* 5x7 self-contained glyphs, column bit 0 = top row. ASCII subset covers UI. */
void c3_lcd_text(int x, int y, const char *s, uint16_t color, int scale)
{
    if (scale<1) scale=1;
    for (;*s;s++,x+=6*scale) {
        const uint8_t *f = c3_font_glyph(*s);
        if (!f) continue;
        /* Aggregate adjacent set pixels in one window per glyph scan row. */
        for (int row=0;row<7;row++) {
            int col=0;
            while (col<5) {
                while (col<5 && !((f[col]>>row)&1u)) col++;
                int begin=col;
                while (col<5 && ((f[col]>>row)&1u)) col++;
                if (col>begin) c3_lcd_rect(x+begin*scale,y+row*scale,
                                            (col-begin)*scale,scale,color);
            }
        }
    }
}
static void decimal5(uint32_t n, char out[6])
{
    if (n>99999u) n=99999u;
    for (int i=4;i>=0;i--) { out[i]=(char)('0'+n%10u); n/=10u; }
    out[5]=0;
}
void c3_lcd_ui(uint32_t count, uint8_t sd_ok, uint8_t grid, uint8_t mono)
{
    char num[6]; decimal5(count,num);
    c3_lcd_text(6,5,"C3M1",0xFFFF,2);
    c3_lcd_text(239,5,mono?"BW":"RAW8",0xFFFF,2);
    c3_lcd_text(90,222,sd_ok?num:"NO CARD",sd_ok?0xFFFF:0xFFE0,1);
    c3_lcd_text(251,222,grid?"GRID":"AUTO",0xFFFF,1);
}
void c3_lcd_message(const char *msg)
{
    c3_lcd_rect(22,97,276,38,0x0000);
    c3_lcd_text(30,108,msg,0xFFFF,2);
}

#ifndef C3_LCD_DISPLAY_H
#define C3_LCD_DISPLAY_H
#include <stdint.h>
#define C3_LCD_W 320u
#define C3_LCD_HEIGHT 240u
/* ST7789 240x320 panel viewed in landscape: MV+MX vs MV+MY.
 * Switching both MX/MY bits gives a true 180-degree flip. */
/* Keep the experimentally corrected ST7789 color settings: BGR=1 and
 * display inversion ON (INVON 0x21).  The C3M1 panel needs inversion on,
 * even though the OEM init excerpt omitted that command. Diagnostics can
 * still switch both settings at runtime without changing FIFO pixel bytes. */
#define C3_LCD_MADCTL_NORMAL (0x60u | (C3_LCD_DEFAULT_BGR ? 0x08u : 0u))
#define C3_LCD_MADCTL_ROT180 (0xA0u | (C3_LCD_DEFAULT_BGR ? 0x08u : 0u))
#define C3_LCD_MADCTL C3_LCD_MADCTL_ROT180
/* FSMC bank 4, A10 selects data register, 8-bit 8080 bus.
 * Always-inline fast path: exactly two byte MMIO stores per RGB565 pixel.
 * Do not change this to uint16_t: production LCD has only 8 data pins.
 */
#define C3_LCD_DATA8 (*(volatile uint8_t *)0x6C000400u)
static inline __attribute__((always_inline)) void c3_lcd_write_byte_fast(uint8_t data)
{
    C3_LCD_DATA8 = data;
}
static inline __attribute__((always_inline)) void c3_lcd_write565_fast(uint16_t rgb565)
{
    c3_lcd_write_byte_fast((uint8_t)(rgb565 >> 8));
    c3_lcd_write_byte_fast((uint8_t)rgb565);
}
void c3_lcd_init(void);
/* Backlight is intentionally enabled only after LCD setup + orientation. */
void c3_lcd_backlight_on(void);
void c3_lcd_backlight_off(void);
void c3_lcd_enter_sleep(void);
void c3_lcd_leave_sleep(void);
/* Immediate hardware GRAM orientation switch, no CPU pixel rotation. */
void c3_lcd_set_rotation(uint8_t rotate_180);
/* Read-only diagnostics: switches only LCD color interpretation. It does not
 * modify the camera/FIFO pixels, byte order, or persistent settings. */
void c3_lcd_set_color_mode(uint8_t bgr, uint8_t inversion_on);
void c3_lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void c3_lcd_pixel_stream(uint16_t rgb565);
void c3_lcd_fill(uint16_t rgb565);
void c3_lcd_rect(int x, int y, int w, int h, uint16_t rgb565);
void c3_lcd_text(int x, int y, const char *s, uint16_t color, int scale);
void c3_lcd_ui(uint32_t count, uint8_t sd_ok, uint8_t grid, uint8_t mono);
void c3_lcd_message(const char *msg);
#endif

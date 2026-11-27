#ifndef C3_LCD_DMA_H
#define C3_LCD_DMA_H
#include <stdint.h>
#if defined(C3_LCD_DMA_HOST_TEST)
/* Host-only I/O emulation; does not affect the release binary. */
#define C3_LCD_W 320u
#define C3_LCD_HEIGHT 240u
void c3_lcd_write_byte_fast(uint8_t byte);
void c3_lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
#else
#include "c3_lcd.h"
#endif

/* v0.1.38 mainline: field-tested LCD DMA is the default. Define 0 to
 * restore the original CPU->FSMC write path without changing FIFO timing. */
#ifndef C3_LCD_DMA_ENABLE
#define C3_LCD_DMA_ENABLE 1
#endif
#if C3_LCD_DMA_ENABLE != 0 && C3_LCD_DMA_ENABLE != 1
#error "C3_LCD_DMA_ENABLE must be 0 or 1"
#endif
#define C3_LCD_DMA_LINE_BYTES (C3_LCD_W * 2u)

/* ST7789 8-bit bus needs RGB565 MSB then LSB. This helper is pure and
 * host-testable; do NOT use 16-bit FSMC writes or byte swapping. */
static inline void c3_lcd_dma_put565(uint8_t *buffer, uint16_t x, uint16_t rgb565)
{
    buffer[(uint32_t)x * 2u] = (uint8_t)(rgb565 >> 8u);
    buffer[(uint32_t)x * 2u + 1u] = (uint8_t)rgb565;
}

#if C3_LCD_DMA_ENABLE
/* DMA2 CH1, software-triggered MEM2MEM. No IRQ, no SDIO channel used.
 * Prepare a full-screen LCD window *before* calling begin(). */
void c3_lcd_dma_begin(void);
uint8_t *c3_lcd_dma_line(uint16_t row);
void c3_lcd_dma_submit(uint16_t row);
void c3_lcd_dma_finish(void);

/* SWD-readable, monotonic counters. A fault permanently switches to CPU
 * for this power cycle. The current line is recovered without rewinding FIFO. */
extern volatile uint32_t c3_lcd_dma_rows_done;
extern volatile uint32_t c3_lcd_dma_rows_cpu;
extern volatile uint32_t c3_lcd_dma_error_count;
extern volatile uint32_t c3_lcd_dma_timeout_count;
extern volatile uint32_t c3_lcd_dma_disabled;
#if C3_STAGE_PROFILE
/* Accumulated CPU blocking cycles in wait_pending during last frame. */
extern volatile uint32_t c3_lcd_dma_wait_cycles;
#endif
#endif
#endif

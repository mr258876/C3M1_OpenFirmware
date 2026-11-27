/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef C3_LCD_DMA_H
#define C3_LCD_DMA_H
#include <stdint.h>
#include "c3_lcd.h"

/* v0.1.38 mainline: field-tested LCD DMA is the default. Define 0 to
 * restore the original CPU->FSMC write path without changing FIFO timing. */
#define C3_LCD_DMA_LINE_BYTES (C3_LCD_W * 2u)

/* ST7789 8-bit bus needs RGB565 MSB then LSB. This helper is pure and
 * host-testable; do NOT use 16-bit FSMC writes or byte swapping. */
static inline void c3_lcd_dma_put565(uint8_t *buffer, uint16_t x, uint16_t rgb565)
{
    buffer[(uint32_t)x * 2u] = (uint8_t)(rgb565 >> 8u);
    buffer[(uint32_t)x * 2u + 1u] = (uint8_t)rgb565;
}

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
#endif

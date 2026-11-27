/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* C3M1 v0.1.31 experimental LCD-only DMA pipeline.
 * DMA2 CH1 reads 8-bit bytes from internal SRAM, repeatedly writes the
 * *same* FSMC LCD data address (0x6C000400). The LCD increments GRAM itself.
 *
 * Ping-pong line buffers allow the CPU to continue reading the AL422B
 * FIFO while DMA sends the previous line. No IRQ is used: the next submit
 * polls completion, and an error/timeout disables DMA for this boot.
 */
#include "c3_lcd_dma.h"
#include "main.h"

_Static_assert(C3_LCD_W == 320u && C3_LCD_HEIGHT == 240u,
               "DMA preview line geometry must match the panel");
_Static_assert(C3_LCD_DMA_LINE_BYTES == 640u, "8-bit RGB565 line size");

static uint8_t line_buf[2][C3_LCD_DMA_LINE_BYTES] __attribute__((aligned(4)));
static uint16_t pending_row;
static uint8_t pending;
static uint8_t initialized;
static uint8_t active;
volatile uint32_t c3_lcd_dma_rows_done;
volatile uint32_t c3_lcd_dma_rows_cpu;
volatile uint32_t c3_lcd_dma_error_count;
volatile uint32_t c3_lcd_dma_timeout_count;
volatile uint32_t c3_lcd_dma_disabled;

#define C3_DMA_LCD_ADDR (0x6C000400u)
#define C3_DMA_MAX_WAIT_MS 10u
/* Additional watchdog if HAL tick is stalled; few hundred microseconds
 * in the ordinary successful case. */
#define C3_DMA_MAX_POLLS 1000000u

static void stop_dma(void)
{
    DMA2_Channel1->CCR &= ~DMA_CCR_EN;
    __DSB();
    DMA2->IFCR = DMA_IFCR_CGIF1;
}

static void disable_dma(uint8_t timed_out)
{
    stop_dma();
    c3_lcd_dma_disabled = 1u;
    ++c3_lcd_dma_error_count;
    if (timed_out) ++c3_lcd_dma_timeout_count;
}

static uint8_t wait_pending_impl(void)
{
    if (!pending) return 1u;
    uint32_t start = HAL_GetTick();
    uint32_t polls = 0u;
    for (;;) {
        uint32_t flags = DMA2->ISR;
        if (flags & DMA_ISR_TEIF1) {
            disable_dma(0u);
            pending = 0u;
            return 0u;
        }
        if (flags & DMA_ISR_TCIF1) {
            stop_dma();
            pending = 0u;
            ++c3_lcd_dma_rows_done;
            return 1u;
        }
        if (((uint32_t)(HAL_GetTick() - start) >= C3_DMA_MAX_WAIT_MS) ||
            (++polls >= C3_DMA_MAX_POLLS)) {
            disable_dma(1u);
            pending = 0u;
            return 0u;
        }
    }
}

/* Only time the CPU blocked on the LCD DMA channel.  The LCD transfer
 * itself overlaps with CPU row preparation and is NOT added twice. */
static uint8_t wait_pending(void)
{
    uint8_t ok = wait_pending_impl();
    return ok;
}

static void cpu_write_line(const uint8_t *src)
{
    for (uint16_t i = 0u; i < C3_LCD_DMA_LINE_BYTES; ++i)
        c3_lcd_write_byte_fast(src[i]);
    ++c3_lcd_dma_rows_cpu;
}

static void recover_failed_line(uint16_t next_row)
{
    /* A failed DMA may have sent an unknown prefix. Readdress just the
     * previous row and repaint from its intact, *other* ping-pong buffer.
     * Then resume at the first pixel of next_row in CPU-only mode. */
    c3_lcd_window(0u, pending_row, C3_LCD_W - 1u, pending_row);
    cpu_write_line(line_buf[pending_row & 1u]);
    if (next_row < C3_LCD_HEIGHT)
        c3_lcd_window(0u, next_row, C3_LCD_W - 1u, C3_LCD_HEIGHT - 1u);
}

void c3_lcd_dma_begin(void)
{
    if (!initialized) {
        __HAL_RCC_DMA2_CLK_ENABLE();
        DMA2_Channel1->CCR = 0u;
        DMA2->IFCR = DMA_IFCR_CGIF1;
        initialized = 1u;
    }
    /* All previous GRAM writes must be complete before a new frame window. */
    pending = 0u;
    active = 1u;
}

uint8_t *c3_lcd_dma_line(uint16_t row)
{
    return line_buf[row & 1u];
}

void c3_lcd_dma_submit(uint16_t row)
{
    if (!active || row >= C3_LCD_HEIGHT) return;
    /* The CPU has already filled row; the previous row uses the opposite
     * buffer and may still be in flight while that fill took place. */
    if (pending && !wait_pending()) recover_failed_line(row);

    if (c3_lcd_dma_disabled) {
        cpu_write_line(line_buf[row & 1u]);
        return;
    }

    /* Source in SRAM is incrementing CMAR, LCD data register is fixed CPAR.
     * MEM2MEM=1 triggers on EN without a peripheral DMA request. PSIZE and
     * MSIZE are zero => BYTE. PINC=0, MINC=1, DIR=1. */
    DMA2_Channel1->CCR = 0u;
    DMA2->IFCR = DMA_IFCR_CGIF1;
    DMA2_Channel1->CPAR = C3_DMA_LCD_ADDR;
    DMA2_Channel1->CMAR = (uint32_t)(uintptr_t)line_buf[row & 1u];
    DMA2_Channel1->CNDTR = C3_LCD_DMA_LINE_BYTES;
    DMA2_Channel1->CCR = DMA_CCR_MEM2MEM | DMA_CCR_DIR |
                         DMA_CCR_MINC | DMA_CCR_PL_0;
    pending_row = row;
    pending = 1u;
    DMA2_Channel1->CCR |= DMA_CCR_EN;
}

void c3_lcd_dma_finish(void)
{
    if (!active) return;
    if (pending && !wait_pending()) recover_failed_line(C3_LCD_HEIGHT);
    /* Allow a final FSMC NWE strobe to complete before any new LCD command.
     * This is not a frame delay; it is a short post-DMA bus settle. */
    __DSB();
    for (volatile uint32_t n = 0u; n < 80u; ++n) __NOP();
    active = 0u;
}

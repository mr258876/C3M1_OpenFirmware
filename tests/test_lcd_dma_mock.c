/* Behavioral emulation of the real DMA driver (same production source).
 * Modes: full transfer, transfer error mid-row, DMA timeout mid-row. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c3_lcd_dma_host_shim.h"
#include "c3_lcd_dma.h"

MockDMA mock_dma2;
MockDMAChannel mock_dma2_ch1;
static uint8_t gram[240][640];
static uint16_t win_x0,win_y0,win_x1,win_y1;
static uint16_t x,y;
static unsigned byte_in_pixel;
static unsigned tick_count;
static int inject_fault;
static uint16_t fault_row;
static unsigned status_windows;
static unsigned dma_starts;

uint32_t HAL_GetTick(void) { return tick_count++; }

void c3_lcd_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1)
{
    assert(x0 <= x1 && x1 < 320u);
    assert(y0 <= y1 && y1 < 240u);
    win_x0=x0;win_y0=y0;win_x1=x1;win_y1=y1;
    x=x0;y=y0;byte_in_pixel=0;
    ++status_windows;
}

void c3_lcd_write_byte_fast(uint8_t byte)
{
    assert(y>=win_y0 && y<=win_y1);
    assert(x>=win_x0 && x<=win_x1);
    gram[y][x*2+byte_in_pixel]=byte;
    ++byte_in_pixel;
    if (byte_in_pixel == 2u) {
        byte_in_pixel=0;
        if (x == win_x1) { x=win_x0; ++y; }
        else ++x;
    }
}

void c3_lcd_dma_host_on_start(const uint8_t *source,uint16_t row)
{
    assert(DMA2_Channel1->CPAR == 0x6C000400u);
    assert(DMA2_Channel1->CNDTR == 640u);
    assert((DMA2_Channel1->CCR & (DMA_CCR_MEM2MEM | DMA_CCR_DIR |
           DMA_CCR_MINC | DMA_CCR_EN)) ==
           (DMA_CCR_MEM2MEM | DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_EN));
    ++dma_starts;
    unsigned n=640u;
    /* The controller may have written a prefix before reporting failure. */
    if (row == fault_row && inject_fault != 0) n=36u;
    for (unsigned i=0u; i<n; ++i) c3_lcd_write_byte_fast(source[i]);
    if (row == fault_row && inject_fault == 1)
        DMA2->ISR = DMA_ISR_TEIF1;
    else if (row == fault_row && inject_fault == 2)
        DMA2->ISR = 0u;
    else
        DMA2->ISR = DMA_ISR_TCIF1;
}

static uint16_t pixel_at(uint16_t row, uint16_t col)
{
    return (uint16_t)(((row*53u+col*5u) ^ (row << 4)) & 0xffffu);
}

int main(int argc,char **argv)
{
    assert(argc == 2);
    int scenario=atoi(argv[1]);
    assert(scenario >= 0 && scenario <= 4);
    inject_fault=(scenario == 0 ? 0 : (scenario == 2 ? 2 : 1));
    fault_row=(uint16_t)(scenario == 3 ? 0u : (scenario == 4 ? 239u : 5u));
    c3_lcd_window(0u,0u,319u,239u);
    c3_lcd_dma_begin();
    for (uint16_t row=0u;row<240u;++row) {
        uint8_t *line=c3_lcd_dma_line(row);
        for (uint16_t col=0u;col<320u;++col)
            c3_lcd_dma_put565(line,col,pixel_at(row,col));
        c3_lcd_dma_submit(row);
    }
    c3_lcd_dma_finish();
    for (unsigned row=0;row<240u; ++row)
        for (unsigned col=0;col<320u;++col) {
            uint16_t exp=pixel_at(row,col);
            assert(gram[row][col*2u] == (uint8_t)(exp >> 8));
            assert(gram[row][col*2u+1] == (uint8_t)exp);
        }
    assert(c3_lcd_dma_error_count == (inject_fault ? 1u : 0u));
    assert(c3_lcd_dma_disabled == (inject_fault ? 1u : 0u));
    assert(c3_lcd_dma_timeout_count == (inject_fault == 2 ? 1u : 0u));
    assert(c3_lcd_dma_rows_done == (inject_fault ? fault_row : 240u));
    assert(c3_lcd_dma_rows_cpu == (inject_fault ? 240u-fault_row : 0u));
    assert(status_windows == (inject_fault ? (fault_row == 239u ? 2u : 3u) : 1u));
    assert(dma_starts == (inject_fault ? fault_row+1u : 240u));
    printf("PASS LCD DMA scenario %d: %u DMA completed, %u CPU fallback, %u errors\n",
           scenario,(unsigned)c3_lcd_dma_rows_done,
           (unsigned)c3_lcd_dma_rows_cpu,(unsigned)c3_lcd_dma_error_count);
    return 0;
}

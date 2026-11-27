#ifndef C3_LCD_DMA_HOST_SHIM_H
#define C3_LCD_DMA_HOST_SHIM_H
#include <stdint.h>

typedef struct { uint32_t CCR, CNDTR, CPAR, CMAR; } MockDMAChannel;
typedef struct { uint32_t ISR, IFCR; } MockDMA;
extern MockDMA mock_dma2;
extern MockDMAChannel mock_dma2_ch1;
#define DMA2 (&mock_dma2)
#define DMA2_Channel1 (&mock_dma2_ch1)
#define DMA_CCR_EN       (1u << 0)
#define DMA_CCR_DIR      (1u << 4)
#define DMA_CCR_MINC     (1u << 7)
#define DMA_CCR_PL_0     (1u << 12)
#define DMA_CCR_MEM2MEM  (1u << 14)
#define DMA_ISR_TCIF1    (1u << 1)
#define DMA_ISR_TEIF1    (1u << 3)
#define DMA_IFCR_CGIF1   (1u << 0)
#define __DSB() ((void)0)
#define __NOP() ((void)0)
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
uint32_t HAL_GetTick(void);
void c3_lcd_dma_host_on_start(const uint8_t *source, uint16_t row);
#endif

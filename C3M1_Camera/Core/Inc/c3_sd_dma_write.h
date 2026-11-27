#ifndef C3_SD_DMA_WRITE_H
#define C3_SD_DMA_WRITE_H
#include <stdint.h>
/* Synchronous SDIO DMA write. TRUE only when data is on-card and card READY. */
uint8_t c3_sd_dma_write_sectors(const uint8_t *data, uint32_t sector, uint32_t count);
#endif

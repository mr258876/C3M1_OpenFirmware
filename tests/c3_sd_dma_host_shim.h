#ifndef C3_SD_DMA_HOST_SHIM_H
#define C3_SD_DMA_HOST_SHIM_H
#include <stdint.h>
#define MSD_OK 0u
#define HAL_SD_ERROR_NONE 0u
#define HAL_DMA_ERROR_NONE 0u
#define HAL_SD_STATE_READY 1u
#define HAL_SD_STATE_BUSY 2u
typedef struct { uint32_t ErrorCode; } DMA_HandleTypeDef;
typedef struct { uint32_t ErrorCode; DMA_HandleTypeDef *hdmatx; } SD_HandleTypeDef;
extern SD_HandleTypeDef hsd;
uint8_t c3_sdio_dma_is_ready(void);
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *data, uint32_t sector, uint32_t count);
uint8_t BSP_SD_GetCardState(void);
uint32_t HAL_GetTick(void);
uint32_t HAL_SD_GetState(SD_HandleTypeDef *h);
uint32_t HAL_DMA_GetError(DMA_HandleTypeDef *h);
uint32_t HAL_SD_Abort(SD_HandleTypeDef *h);
#endif

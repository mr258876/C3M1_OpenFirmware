/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* v0.1.68 experimental SDIO DMA2_CH4 sector writer for FatFs.
 * FatFs remains synchronous. DMA replaces CPU FIFO feeding, NOT SD busy wait.
 * In-flight buffer is immutable until HAL SD & card state signal completion.
 * A failed DMA must never be silently retried with polling at the same LBA:
 * partial writes and filesystem metadata could already be on the card. */
#include "c3_sd_dma_write.h"
#include <string.h>
#include "sdio.h"
#include "bsp_driver_sd.h"

#define C3_SD_DMA_SECTOR_BYTES 512u
#define C3_SD_DMA_MAX_SECTORS 128u /* 128*512/4 = 16384 < 65535 DMA words */
#define C3_SD_DMA_BOUNCE_SECTORS 8u
#define C3_SD_DMA_TIMEOUT_MS 3000u
static uint8_t bounce[C3_SD_DMA_BOUNCE_SECTORS*C3_SD_DMA_SECTOR_BYTES]
    __attribute__((aligned(4)));

static void abort_dma(void)
{
    /* Hardware may have written part of a sector. Make next operation fail
     * rather than incorrectly reporting completion or reissuing the write. */
    (void)HAL_SD_Abort(&hsd);
}

static uint8_t dma_done_and_card_ready(void)
{
    uint32_t start=HAL_GetTick();
    while (HAL_SD_GetState(&hsd)!=HAL_SD_STATE_READY) {
        if (hsd.ErrorCode!=HAL_SD_ERROR_NONE) return 0u;
        if ((uint32_t)(HAL_GetTick()-start)>=C3_SD_DMA_TIMEOUT_MS) return 0u;
    }
    if (hsd.ErrorCode!=HAL_SD_ERROR_NONE || !hsd.hdmatx ||
        HAL_DMA_GetError(hsd.hdmatx)!=HAL_DMA_ERROR_NONE)
        return 0u;
    /* DMA complete does not imply the SD card has finished programming. */
    while (BSP_SD_GetCardState()!=MSD_OK) {
        if (hsd.ErrorCode!=HAL_SD_ERROR_NONE) return 0u;
        if ((uint32_t)(HAL_GetTick()-start)>=C3_SD_DMA_TIMEOUT_MS) return 0u;
    }
    return (uint8_t)(hsd.ErrorCode==HAL_SD_ERROR_NONE);
}

uint8_t c3_sd_dma_write_sectors(const uint8_t *data,
                                 uint32_t sector,uint32_t count)
{
    if (!data || !count || !c3_sdio_dma_is_ready()) return 0u;
    while (count) {
        uint32_t take=count>C3_SD_DMA_MAX_SECTORS ? C3_SD_DMA_MAX_SECTORS : count;
        const uint8_t *src=data;
        if (((uintptr_t)data & 3u)!=0u) {
            /* FatFs may pass odd-aligned byte pointers. DMA requires 32-bit
             * alignment. Only use the 4KB bounce for these rare cases. */
            if (take>C3_SD_DMA_BOUNCE_SECTORS) take=C3_SD_DMA_BOUNCE_SECTORS;
            memcpy(bounce,data,take*C3_SD_DMA_SECTOR_BYTES);
            src=bounce;
        }
        if (BSP_SD_WriteBlocks_DMA((uint32_t *)(uintptr_t)src,
                                    sector,take)!=MSD_OK) {
            abort_dma();
            return 0u;
        }
        if (!dma_done_and_card_ready()) {
            abort_dma();
            return 0u;
        }
        data+=take*C3_SD_DMA_SECTOR_BYTES;
        sector+=take;
        count-=take;
    }
    return 1u;
}

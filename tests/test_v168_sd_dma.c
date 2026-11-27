/* Exercising EXACT production c3_sd_dma_write.c, including synchronous wait
 * and unaligned DMA source. No actual STM32 peripherals are simulated. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_sd_dma_host_shim.h"
#include "c3_sd_dma_write.h"

static DMA_HandleTypeDef dma;
SD_HandleTypeDef hsd;
static uint8_t ready,fail_start,make_error;
static unsigned busy_hal,busy_card;
static uint32_t mock_tick;
static unsigned writes,aborts,card_polls;
static uint32_t sectors[8],sizes[8];
static const uint8_t *source_base;
static uint32_t first_sector;

uint8_t c3_sdio_dma_is_ready(void){return ready;}
uint32_t HAL_GetTick(void){return mock_tick++;}
uint32_t HAL_SD_Abort(SD_HandleTypeDef *h){(void)h;++aborts;return 0u;}
uint32_t HAL_DMA_GetError(DMA_HandleTypeDef *h){return h->ErrorCode;}
uint32_t HAL_SD_GetState(SD_HandleTypeDef *h)
{
    if (make_error) h->ErrorCode=9u;
    if (busy_hal) {--busy_hal; return HAL_SD_STATE_BUSY;}
    return HAL_SD_STATE_READY;
}
uint8_t BSP_SD_GetCardState(void)
{
    ++card_polls;
    if (busy_card) {--busy_card;return 1u;}
    return MSD_OK;
}
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *data,uint32_t sector,uint32_t count)
{
    assert(((uintptr_t)data&3u)==0u);
    assert(writes < 8u);
    sectors[writes]=sector;
    sizes[writes]=count;
    uint32_t off=(sector-first_sector)*512u;
    assert(memcmp(data,source_base+off,count*512u)==0);
    ++writes;
    return fail_start ? 1u : MSD_OK;
}
static void reset(void)
{
    hsd.ErrorCode=0u;
    dma.ErrorCode=0u;
    hsd.hdmatx=&dma;
    ready=1u;fail_start=0u;make_error=0u;
    busy_hal=2u;busy_card=2u;
    writes=0u;aborts=0u;mock_tick=0u;card_polls=0u;
    memset(sectors,0,sizeof(sectors));memset(sizes,0,sizeof(sizes));
}
int main(void)
{
    static uint32_t data32[(140u*512u)/4u];
    uint8_t *aligned=(uint8_t*)data32;
    for (unsigned i=0;i<sizeof(data32);++i) aligned[i]=(uint8_t)(i*7u+3u);
    reset();first_sector=100u;source_base=aligned;
    assert(c3_sd_dma_write_sectors(aligned,100u,140u));
    assert(writes==2u && sizes[0]==128u && sizes[1]==12u);
    assert(sectors[0]==100u && sectors[1]==228u && aborts==0u);
    assert(card_polls>0u);

    static uint8_t unaligned_data[6000u+4u];
    uint8_t *odd=unaligned_data+1u;
    for (unsigned i=0;i<11u*512u;++i) odd[i]=(uint8_t)(i*5u+1u);
    reset();first_sector=42u;source_base=odd;
    assert(c3_sd_dma_write_sectors(odd,42u,11u));
    assert(writes==2u && sizes[0]==8u && sizes[1]==3u);
    assert(sectors[1]==50u && aborts==0u);

    reset();ready=0u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==0u && aborts==0u);
    reset();fail_start=1u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==1u && aborts==1u);
    reset();make_error=1u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==1u && aborts==1u);
    reset();dma.ErrorCode=1u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==1u && aborts==1u);
    reset();busy_hal=5000u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==1u && aborts==1u && card_polls==0u);
    reset();busy_card=5000u;first_sector=1u;source_base=aligned;
    assert(!c3_sd_dma_write_sectors(aligned,1u,1u));
    assert(writes==1u && aborts==1u && card_polls>0u);
    puts("PASS v168 DMA: aligned multi, unaligned bounce, DMA/SD card wait, fail/start/timeout/abort");
    return 0;
}

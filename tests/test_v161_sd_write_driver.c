/* Run the actual FatFs sd_diskio.c write function with a mocked SD controller.
 * The indicator must encompass both transfer and busy-poll, and close on ALL
 * error exits, without extending the SD transfer itself. */
#include "ff_gen_drv.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
extern const Diskio_drvTypeDef SD_Driver;
extern DRESULT SD_write(BYTE,const BYTE *,DWORD,UINT);
static unsigned begins,ends,blocks,busy_calls;
static uint8_t active;
static unsigned busy_before_ready;
static int force_bsp_fail,force_never_ready;
void c3_sd_led_write_begin(void) { assert(!active); ++begins; active=1u; }
void c3_sd_led_write_end(void) { assert(active); ++ends; active=0u; }
uint8_t BSP_SD_Init(void) { return MSD_OK; }
uint8_t BSP_SD_GetCardState(void)
{
    ++busy_calls;
    if (force_never_ready || busy_before_ready) {
        if (busy_before_ready) --busy_before_ready;
        return MSD_ERROR;
    }
    return MSD_OK;
}
uint8_t BSP_SD_ReadBlocks(uint32_t *p,uint32_t addr,uint32_t n,uint32_t timeout)
{ (void)p; (void)addr; (void)n; (void)timeout; assert(!active); return MSD_OK; }
uint8_t BSP_SD_WriteBlocks(uint32_t *p,uint32_t addr,uint32_t n,uint32_t timeout)
{
    (void)p; (void)addr; (void)n; (void)timeout;
    assert(active);
    ++blocks;
    return force_bsp_fail ? MSD_ERROR : MSD_OK;
}
static void reset(void)
{ begins=ends=blocks=busy_calls=0u; active=0u; busy_before_ready=0u;
  force_bsp_fail=force_never_ready=0; }
int main(void)
{
    static const BYTE data[512]={0};
    assert(SD_Driver.disk_write==SD_write);
    reset();
    busy_before_ready=3u;
    assert(SD_write(0u,data,0u,1u)==RES_OK);
    assert(begins==1u && ends==1u && blocks==1u && busy_calls==4u && !active);
    reset();
    force_bsp_fail=1;
    assert(SD_write(0u,data,1u,1u)==RES_ERROR);
    assert(begins==1u && ends==1u && blocks==1u && busy_calls==0u && !active);
    reset();
    force_never_ready=1;
    assert(SD_write(0u,data,2u,1u)==RES_ERROR);
    assert(begins==1u && ends==1u && blocks==1u && busy_calls>=100000u && !active);
    reset();
    assert(SD_Driver.disk_initialize(0u)==0u);
    assert(begins==0u && ends==0u); /* mounting does not light the LED */
    assert(SD_Driver.disk_read(0u,(BYTE*)data,0u,1u)==RES_OK);
    assert(begins==0u && ends==0u); /* reading does not light the LED */
    puts("v161 SD_write driver: OK / HAL failure / busy timeout / read ignored PASS");
    return 0;
}

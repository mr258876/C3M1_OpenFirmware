from pathlib import Path
r=Path(__file__).resolve().parents[1]
main=(r/'C3M1_Camera/Core/Src/main.c').read_text()
build=(r/'build_clang.sh').read_text()
sdio=(r/'C3M1_Camera/Core/Src/sdio.c').read_bytes().decode('latin1')
sd=(r/'C3M1_Camera/Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c').read_text(encoding='latin1')
hal=(r/'C3M1_Camera/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sd.c').read_text()
assert 'C3_NO_SAVED:-1' in build and 'C3_SDIO_TX_DMA:-1' in build
assert 'C3_SDIO_TX_DMA' in sd and 'c3_sd_dma_write_sectors' in sd
assert 'C3_NO_SAVED' in main and '#if !C3_NO_SAVED' in main
assert 'c3_lcd_message("SAVED")' in main and main.index('#if !C3_NO_SAVED') < main.index('c3_lcd_message("SAVED")')
assert 'DMA2_Channel4' in sdio and 'DMA2_Channel4_5_IRQHandler' in sdio
assert 'HAL_DMA_IRQHandler(&c3_sd_tx_dma)' in sdio and 'HAL_SD_IRQHandler(&hsd)' in sdio
assert 'DMA_PDATAALIGN_WORD' in sdio and 'DMA_MDATAALIGN_WORD' in sdio
assert 'DMA2_Channel1' not in sdio
assert 'RxErrorCode = hsd->hdmarx ? hsd->hdmarx->ErrorCode : HAL_DMA_ERROR_NONE' in hal
sd_module=(r/'C3M1_Camera/Core/Src/c3_sd_dma_write.c').read_text()
assert 'BSP_SD_WriteBlocks_DMA' in sd_module and 'HAL_SD_GetState(&hsd)' in sd_module
assert 'BSP_SD_GetCardState()!=MSD_OK' in sd_module and 'HAL_SD_Abort(&hsd)' in sd_module
assert 'memcpy(bounce,data,take*C3_SD_DMA_SECTOR_BYTES)' in sd_module
assert 'Core/Src/c3_sd_dma_write.c' in build
print('PASS v168: DMA2_CH4 / IRQ wiring, no-SAVED default, card-ready and error guard')

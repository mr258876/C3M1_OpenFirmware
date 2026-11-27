#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
P="$PWD/C3M1_Camera"
O="$PWD/build"
mkdir -p "$O"
CC="${CLANG:-clang}"
OBJCOPY="${LLVM_OBJCOPY:-llvm-objcopy}"
I=( -I"$P/Core/Inc" -I"$P/FATFS/App" -I"$P/FATFS/Target"
    -I"$P/Middlewares/Third_Party/FatFs/src" -I"$P/Middlewares/Third_Party/FatFs/src/drivers"
    -I"$P/Drivers/BSP/OV7725" -I"$P/Drivers/SYSTEM/sys"
    -I"$P/Drivers/STM32F1xx_HAL_Driver/Inc" -I"$P/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy"
    -I"$P/Drivers/CMSIS/Device/ST/STM32F1xx/Include" -I"$P/Drivers/CMSIS/Include" -I"$PWD/compat")
CFLAGS=( --target=arm-none-eabi
  -mcpu=cortex-m3
  -mthumb
  -std=gnu11
  -Os
  -g
  -ffunction-sections
  -fdata-sections
  -ffreestanding
  -fno-builtin
  -DSTM32F103xE
  -DUSE_HAL_DRIVER
  -Wno-unused-parameter
  -DC3_FIFO_DMA_LIVE_ROWS=4
  -DC3_FIFO_DMA_LIVE_TICKS=14
  -flto=full
  -DC3_LCD_POWER_SETTLE_MS=500u
  -DC3_CAM_POWER_SETTLE_MS=1000u
  -DC3_SCCB_MAIN_HALF_US=50u
  -DC3_CAM_COM3_RGB=0xD0u
  -DC3_DNG_BLACK_CALIBRATION=1
  -DC3_SDIO_TX_DMA=1
  -DC3_FAST_BMP_ENABLE=1
  -DC3_RAW_CLKRC=0
  -DC3_DNG_SETTLE_LOCKED=6
  -DC3_DNG_FAST_SETTLE_FRAMES=2
  -DC3_RGB_EXHCL=0x80u
  -DC3_SENSOR_PCLK_HZ=12000000u
  -DC3_LCD_DEFAULT_BGR=1
  -DC3_LCD_DEFAULT_INV_ON=1
  -DC3_FSMC_DATAST=4
)
FILES=(
  Core/Src/main.c
  Core/Src/c3_i18n.c
  Core/Src/gpio.c
  Core/Src/fsmc.c
  Core/Src/sdio.c
  Core/Src/stm32f1xx_it.c
  Core/Src/system_stm32f1xx.c
  Core/Src/c3_camera.c
  Core/Src/c3_sd_dma_write.c
  Core/Src/c3_sleep.c
  Core/Src/c3_battery.c
  Core/Src/c3_lcd_dma.c
  Core/Src/c3_zoom.c
  Core/Src/c3_frame_sched.c
  Core/Src/c3_lcd.c
  Core/Src/c3_font.c
  Core/Src/c3_osd.c
  Core/Src/c3_focus.c
  Core/Src/c3_fps.c
  Core/Src/c3_buttons.c
  Core/Src/c3_input.c
  Core/Src/c3_exposure.c
  Core/Src/c3_meter.c
  Core/Src/c3_ui_nav.c
  Core/Src/c3_settings.c
  Core/Src/c3_settings_flash.c
  Core/Src/c3_menu.c
  Core/Src/c3_capture_recovery.c
  Core/Src/c3_storage.c
  Core/Src/c3_dng.c
  Core/Src/c3_bmp.c
  Core/Src/c3_play_decode.c
  Core/Src/c3_playback.c
  Drivers/SYSTEM/delay/delay.c
  Drivers/BSP/EXTI/exti.c
  Drivers/BSP/OV7725/sccb.c
  Drivers/BSP/OV7725/ov7725.c
  FATFS/App/fatfs.c
  FATFS/Target/bsp_driver_sd.c
  Middlewares/Third_Party/FatFs/src/ff.c
  Middlewares/Third_Party/FatFs/src/diskio.c
  Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
  Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sram.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_fsmc.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sd.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_sdmmc.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
  Core/Src/c3_shutter_timing.c
)
objects=()
for file in "${FILES[@]}" "$PWD/compat/runtime.c" "$P/Core/Startup/startup_stm32f103zetx.s"; do
  if [[ "$file" = /* ]]; then src="$file"; else src="$P/$file"; fi
  obj="$O/$(basename "$src").o"
  opts=()
  case "$file" in
    Core/Src/c3_camera.c) opts=(-O3);;
    *.s|*/compat/runtime.c) opts=(-fno-lto);;
  esac
  if [[ "$src" == *.s ]]; then
    "$CC" --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -g -c "$src" -o "$obj"
  else
    "$CC" "${CFLAGS[@]}" "${opts[@]}" "${I[@]}" -c "$src" -o "$obj" -MMD -MF "$obj.d"
  fi
  objects+=("$obj")
done
"$CC" --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdlib -fuse-ld=lld \
  -flto=full -Wl,--lto-O2 -Wl,--gc-sections -Wl,-Map="$O/c3m1.map" -Wl,--print-memory-usage \
  -T linker_lld.ld "${objects[@]}" -o "$O/c3m1.elf"
"$OBJCOPY" -O binary "$O/c3m1.elf" "$O/c3m1.bin"
"$OBJCOPY" -O ihex "$O/c3m1.elf" "$O/c3m1.hex"
ls -lh "$O/c3m1."{elf,bin,hex}

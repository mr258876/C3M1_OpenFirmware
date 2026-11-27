from pathlib import Path
p = Path(__file__).resolve().parent.parent
main = (p/'C3M1_Camera/Core/Src/main.c').read_text()
cam = (p/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
dma = (p/'C3M1_Camera/Core/Src/c3_lcd_dma.c').read_text()
osd = (p/'C3M1_Camera/Core/Src/c3_osd.c').read_text()
build = (p/'build_clang.sh').read_text()

assert 'C3_LCD_DMA_ENABLE:-1' in build
assert '-DC3_LCD_DMA_ENABLE=' in build
assert 'Core/Src/c3_lcd_dma.c' in build
assert '#if C3_LCD_DMA_ENABLE' in cam and '#else' in cam and '#endif /* C3_LCD_DMA_ENABLE */' in cam
assert 'c3_lcd_dma_line(y)' in cam and 'c3_lcd_dma_submit(y)' in cam
assert 'c3_lcd_dma_finish()' in cam
assert cam.index('c3_lcd_dma_finish();') < cam.index('OV7725_OE(1);', cam.index('void c3_camera_preview_draw'))
assert 'C3_DMA_LCD_ADDR (0x6C000400u)' in dma
assert 'DMA2_Channel1->CPAR = C3_DMA_LCD_ADDR;' in dma
assert 'DMA_CCR_MEM2MEM | DMA_CCR_DIR' in dma
assert 'DMA_CCR_MINC | DMA_CCR_PL_0' in dma
assert 'DMA_CCR_PINC' not in dma
assert 'DMA2_Channel1->CNDTR = C3_LCD_DMA_LINE_BYTES;' in dma
assert 'c3_lcd_window(0u, pending_row,' in dma
assert 'c3_lcd_window(0u, next_row,' in dma
assert 'c3_lcd_dma_disabled = 1u;' in dma
assert 'c3_lcd_dma_rows_done' in dma
assert 'c3_lcd_dma_error_count' in dma
# OSD only hides diagnostic text. Fault detection/fallback in DMA driver remains.
assert 'dma_disabled ? "CPU" : "DMA"' not in osd
assert 'text(220, 5, dma_disabled' not in osd
assert 'c3_osd_set_battery' in osd
# The merged LCD-DMA mainline retains the verified GPIO FIFO reader.
assert 'GPIOB->BRR = GPIO_PIN_4;' in (p/'C3M1_Camera/Core/Inc/c3_camera.h').read_text()
assert 'GPIOG->IDR >> 1' in (p/'C3M1_Camera/Core/Inc/c3_camera.h').read_text()
# Do not silently change the validated camera/LCD color modes.
assert 'C3_CAM_COM3_RGB 0xD0u' in main
lcd = (p/'C3M1_Camera/Core/Inc/c3_lcd.h').read_text()
assert '#define C3_LCD_DEFAULT_INV_ON 1' in lcd
assert '#define C3_LCD_DEFAULT_BGR 1' in lcd
print('PASS: LCD-only DMA2/CMAR/CPAR/byte-width, OSD status, RGB565, CPU fallback and GPIO safety')

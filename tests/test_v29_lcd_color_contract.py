#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'C3M1_Camera'
h=(root/'Core/Inc/c3_lcd.h').read_text()
l=(root/'Core/Src/c3_lcd.c').read_text()
m=(root/'Core/Src/main.c').read_text()
o=(root/'Drivers/BSP/OV7725/ov7725.c').read_text()
assert '#define C3_LCD_DEFAULT_BGR 1' in h
assert '#define C3_LCD_DEFAULT_INV_ON 1' in h
assert 'lcd_bgr ? 0x08u : 0u' in l
assert 'cmd(lcd_inversion_on ? 0x21u : 0x20u)' in l
assert 'data(lcd_madctl())' in l
assert 'c3_lcd_set_color_mode' in l
assert 'c3_color_diag_overlay' in m
assert 'C3_BTN_4X' in m and 'C3_BTN_ISO' in m
assert 'cam_com3 ^= 0x20u' in m
assert 'c3_camera_preview_stop();' in m
assert '#define C3_CAM_COM3_RGB 0xD0u' in o
assert '(C3_CAM_COM3_RGB != 0xD0u) && (C3_CAM_COM3_RGB != 0xF0u)' in o
assert 'ov7725_write_reg(COM3, (uint8_t)(C3_CAM_COM3_RGB' in o
assert 'uint16_t hi = c3_camera_fifo_read_fast()' in (root/'Core/Src/c3_camera.c').read_text()
raw=o[o.index('uint8_t ov7725_RAW_init(void)'):o.index('#ifndef C3_FAST_RAW_SWITCH',o.index('uint8_t ov7725_RAW_init(void)'))]
assert 'C3_CAM_COM3_RGB' not in raw
assert 'if (!verify_static_reg(MTX1' in raw
print('PASS: v29 LCD BGR1 + INV1 panel defaults, live switch, mirror-only COM3, RAW strict')

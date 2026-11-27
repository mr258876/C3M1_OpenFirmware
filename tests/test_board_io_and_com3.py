#!/usr/bin/env python3
"""Validate board-level constants and color A/B features against schematics.
Static/host tests do NOT prove physical RGB colors or LCD reset timing.
"""
from pathlib import Path
import re
r = Path(__file__).resolve().parents[1] / 'C3M1_Camera'
h = (r/'Core/Inc/c3_board_pins.h').read_text()
g = (r/'Core/Src/gpio.c').read_text()
l = (r/'Core/Src/c3_lcd.c').read_text()
m = (r/'Core/Src/main.c').read_text()
b = (r/'Core/Src/c3_buttons.c').read_text()
i = (r/'Core/Src/c3_input.c').read_text()
u = (r/'Core/Src/c3_ui_nav.c').read_text()
o = (r/'Drivers/BSP/OV7725/ov7725.c').read_text()
for symbol, bit in [('C3_BTN_ISO',1),('C3_BTN_SHUTTER',2),('C3_BTN_4X',3),('C3_BTN_FILM',10),('C3_LCD_EN_PIN',4),('C3_LCD_RESET_PIN',5)]:
    assert re.search(r'#define\s+' + symbol + r'\s+\(1u\s*<<\s*' + str(bit) + r'\)', h), symbol
assert 'PF0 = NC; PF1 = KEYISO; PF2 = KEYSHOT; PF3 = KEY4X' in h
assert 'g.Pin = C3_LCD_EN_PIN | C3_LCD_RESET_PIN; g.Mode = GPIO_MODE_OUTPUT_PP;' in g
assert 'g.Pin = C3_BTN_MASK;' in g
assert g.index('g.Pin = C3_LCD_EN_PIN | C3_LCD_RESET_PIN') < g.index('g.Pin = C3_BTN_MASK')
assert 'HAL_GPIO_WritePin(GPIOF, GPIO_PIN_3 | GPIO_PIN_4' not in g
assert 'C3_BTN_SHUTTER' in m and 'C3_BTN_ISO' in m and 'C3_BTN_4X' in m
assert 'C3_LCD_EN_PIN' in l and 'C3_LCD_RESET_PIN' in l
assert 'C3_BTN_ISO, C3_BTN_SHUTTER, C3_BTN_4X, C3_BTN_FILM' in b
assert '#define INPUT_MASK C3_BTN_MASK' in i
assert '#define SHOT C3_BTN_SHUTTER' in u
assert '#define ZOOM C3_BTN_4X' in u
assert 'C3_CAM_COM3_RGB 0xD0u' in o
assert 'ov7725_write_reg(COM3, (uint8_t)(C3_CAM_COM3_RGB' in o
assert 'C3_CAM_COLOR_BAR ? 0x01u' in o
assert 'C3_COLOR_SWATCH_DIAG' in m
assert '0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0xFFE0u' in m
# RAW init remains strict: should not write an RGB COM3 orientation into RAW.
raw=o[o.index('uint8_t ov7725_RAW_init(void)'):o.index('#ifndef C3_FAST_RAW_SWITCH',o.index('uint8_t ov7725_RAW_init(void)'))]
assert 'C3_CAM_COM3_RGB' not in raw
assert 'if (!verify_static_reg(MTX1' in raw
print('PASS: production PF1/2/3 PF4/5 pin contract, preview COM3 A/B, RAW strict')

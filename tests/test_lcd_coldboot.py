#!/usr/bin/env python3
"""Source-structure guard for LCD cold-boot GPIO latch/first-init order."""
from pathlib import Path
p=Path(__file__).resolve().parents[1]
g=(p/'C3M1_Camera/Core/Src/gpio.c').read_text()
m=(p/'C3M1_Camera/Core/Src/main.c').read_text()
l=(p/'C3M1_Camera/Core/Src/c3_lcd.c').read_text()
assert '#define C3_LCD_POWER_SETTLE_MS 1000u' in m
assert g.index('HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN | C3_LCD_RESET_PIN, GPIO_PIN_RESET)') < g.index('g.Pin = C3_LCD_EN_PIN | C3_LCD_RESET_PIN; g.Mode = GPIO_MODE_OUTPUT_PP')
start=m.index('int main(void)')
m=m[start:]
assert m.index('MX_GPIO_Init();') < m.index('MX_FSMC_Init();') < m.index('HAL_Delay(C3_LCD_POWER_SETTLE_MS);') < m.index('c3_lcd_init();') < m.index('c3_lcd_set_rotation(settings.rotate_180);')
assert 'c3_boot_stage = 0x11u' in m
assert l.index('HAL_GPIO_WritePin(GPIOF, C3_LCD_RESET_PIN, GPIO_PIN_SET)') > l.index('HAL_GPIO_WritePin(GPIOF, C3_LCD_RESET_PIN, GPIO_PIN_RESET)')
# PF4 MUST remain low until the last rotation and boot glyph are committed.
assert 'HAL_GPIO_WritePin(GPIOF, C3_LCD_EN_PIN, GPIO_PIN_SET);' in l
init_body=l[l.index('void c3_lcd_init(void)'):l.index('void c3_lcd_backlight_on(void)')]
assert 'GPIO_PIN_SET' not in init_body.split('c3_lcd_fill(0x0000);')[-1]
assert m.index('c3_lcd_set_rotation(settings.rotate_180);') < m.index('c3_lcd_message("C3M1 BOOT");') < m.index('c3_lcd_backlight_on();')
assert 'cmd(0x28u)' in l and 'cmd(0x10u)' in l and 'cmd(0x11u)' in l
assert l.index('c3_lcd_fill(0x0000u);',l.index('void c3_lcd_leave_sleep')) < l.index('cmd(0x29u)',l.index('void c3_lcd_leave_sleep')) < l.index('c3_lcd_backlight_on();',l.index('void c3_lcd_leave_sleep'))
print('LCD cold-boot source-order guard: PASS')

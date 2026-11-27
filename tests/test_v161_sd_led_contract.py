#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[1]
g=(r/'C3M1_Camera/Core/Src/gpio.c').read_text()
s=(r/'C3M1_Camera/Core/Src/c3_sleep.c').read_text()
h=(r/'C3M1_Camera/Core/Inc/c3_sleep.h').read_text()
m=(r/'C3M1_Camera/Core/Src/main.c').read_text()
d=(r/'C3M1_Camera/Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c').read_text()
assert 'HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_SET);' in g
assert g.index('HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_SET);') < g.index('g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_OD;')
assert 'GPIO_PIN_11 | GPIO_PIN_9' not in g
assert '#define C3_LED_ON()  (GPIOD->BRR = GPIO_PIN_9)' in s
assert '#define C3_LED_OFF() (GPIOD->BSRR = GPIO_PIN_9)' in s
stop=s[s.index('void c3_sleep_led_stop(void)'):s.index('void c3_sd_led_write_begin(void)')]
assert 'C3_LED_OFF();' in stop and 'C3_LED_ON();' not in stop
assert 'c3_sd_led_poll(HAL_GetTick());' in m
assert m.count('c3_sd_led_poll(HAL_GetTick());')==1
assert 'void c3_sd_led_write_begin(void)' in h and 'void c3_sd_led_write_end(void)' in h
assert 'C3_SD_LED_HOLD_MS 180u' in h
section=d[d.index('DRESULT SD_write('):d.index('#endif /* _USE_WRITE == 1 */',d.index('DRESULT SD_write('))]
assert section.count('c3_sd_led_write_begin();')==1
assert section.count('c3_sd_led_write_end();')==2
assert section.index('c3_sd_led_write_begin();') < section.index('BSP_SD_WriteBlocks(')
assert section.index('c3_sd_led_write_end();') > section.index('BSP_SD_GetCardState()')
assert 'HAL_Delay' not in section and 'C3_LED_ON' not in d
assert 'c3_sleep_led_start();' in m and 'c3_sleep_led_stop();' in m
assert 'c3_sleep_led_irq();' in (r/'C3M1_Camera/Core/Src/stm32f1xx_it.c').read_text()
print('PASS v161: GPIO initially OFF, sector write driver owns LED, wake stops PWM dark, no blocking LED delay')

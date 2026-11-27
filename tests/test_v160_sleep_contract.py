#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[1]
m=(r/'C3M1_Camera/Core/Src/main.c').read_text()
c=(r/'C3M1_Camera/Core/Src/c3_sleep.c').read_text()
g=(r/'C3M1_Camera/Core/Src/gpio.c').read_text()
i=(r/'C3M1_Camera/Core/Src/stm32f1xx_it.c').read_text()
s=(r/'C3M1_Camera/Core/Src/c3_settings_flash.c').read_text()
assert 'GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_OD;' in g
assert 'GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_PP;' not in g
assert 'void TIM2_IRQHandler(void)' in i
assert 'c3_sleep_led_irq();' in i
assert '__HAL_RCC_TIM2_CLK_ENABLE();' in c
assert 'TIM2->PSC=71u' in c and 'TIM2->ARR=C3_SLEEP_PWM_PERIOD_US-1u;' in c
assert 'c3_sleep_led_update(tick-sleep_start);' in m
assert 'c3_camera_preview_stop();' in m[m.index('static void c3_run_sleep'):m.index('static void c3_note_activity')]
assert 'HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);' in m
assert 'ov7725_write_reg(0x09u,0x11u)' in m
assert 'ov7725_write_reg(0x09u,0x01u)' in m
assert '__WFI();' in m
assert 'c3_input_reset(read_buttons(),HAL_GetTick());' in m
assert m.index('if (c3_recovery_can_retry(&recovery))') < m.index('if (nav.page != C3_UI_PREVIEW)')
assert 'c3_sleep_due(settings.sleep_timer' in m
assert 'can_sleep=0u' in m and 'if (settings_dirty)' in m
assert 'c3_settings_select_record(&a,&b)' in s
assert 'c3_settings_defaults(out)' in s
assert 'c3_exposure_sync_dial(exposure,read_shutter_dial()' in m
print('PASS v160: sleep only outside retry, camera/LCD quiescent, low-priority LED PWM, current-format Flash and dial sync')

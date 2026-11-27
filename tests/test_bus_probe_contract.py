from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'C3M1_Camera/Core/Src/c3_fifo_probe.c').read_text()
g=(p/'C3M1_Camera/Core/Src/gpio.c').read_text()
m=(p/'C3M1_Camera/Core/Src/main.c').read_text()
assert 'g.Pin = 0x01FEu' in s, 'input pull test must cover PG1..PG8'
assert 'g.Mode = GPIO_MODE_INPUT' in s
assert 'GPIOG->BSRR = GPIO_PIN_15; /* /OE HIGH' in s
assert 'GPIOG->BRR = GPIO_PIN_15; /* enable FIFO output drive */' in s
assert 'GPIOG->IDR >> 1' in s
assert 'c3_fp_off_pd = bus_sample(0u,0u' in s
assert 'c3_fp_off_pu = bus_sample(0u,1u' in s
assert 'c3_fp_on_pd = bus_sample(1u,0u' in s
assert 'c3_fp_on_pu = bus_sample(1u,1u' in s
assert 'c3_fp_we_enabled = sample_write_gate(1u,220u)' in s
assert 'HAL_NVIC_DisableIRQ(EXTI9_5_IRQn)' in s
assert 'HAL_NVIC_EnableIRQ(EXTI9_5_IRQn)' in s
assert 'g.Pin = GPIO_PIN_10; g.Mode = GPIO_MODE_INPUT' in g
assert 'C3_LCD_POWER_SETTLE_MS 1000u' in m
assert 'c3_fifo_probe_run' in m
print('PASS: v0.1.23 GPIO bus /OE pull-state and /WE gate probe contract')

#!/usr/bin/env python3
"""Static regression on production board control-signal mapping."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]/'C3M1_Camera'
h=(r/'Core/Inc/main.h').read_text()
g=(r/'Core/Src/gpio.c').read_text()
c=(r/'Core/Src/c3_camera.c').read_text()
ch=(r/'Core/Inc/c3_camera.h').read_text()
f=(r/'Core/Src/c3_fifo_probe.c').read_text()
checks={
  'OE pin PG15': '#define OV7725_OE_Pin GPIO_PIN_15' in h and '#define OV7725_OE_GPIO_Port GPIOG' in h,
  'WEN pin PB3': '#define OV7725_WEN_Pin GPIO_PIN_3' in h,
  'RCLK pin PB4': '#define OV7725_RCLK_Pin GPIO_PIN_4' in h,
  'PG15 GPIO initialized': 'g.Pin = GPIO_PIN_14 | GPIO_PIN_15' in g,
  'WEN IRQ PB3': 'if (c3_sched_vsync(&frame_sched)) GPIOB->BSRR = GPIO_PIN_3;' in c,
  'fast read PB4': 'GPIOB->BRR = GPIO_PIN_4;' in ch and 'GPIOB->BSRR = GPIO_PIN_4;' in ch,
  'probe uses real /OE': 'GPIOG->BRR = GPIO_PIN_15' in f and 'GPIOG->BSRR = GPIO_PIN_15' in f,
  'probe uses real RCLK': 'GPIOB->BRR = GPIO_PIN_4' in f and 'GPIOB->BSRR = GPIO_PIN_4' in f,
  'never toggle PB5': not any('GPIO_PIN_5' in line for line in (c+ch+f+g).splitlines() if 'GPIOB->' in line),
  'probe WEN PB3': 'if (on) GPIOB->BSRR = GPIO_PIN_3;' in f,
}
for desc,ok in checks.items():
    print(('PASS' if ok else 'FAIL')+' '+desc)
if not all(checks.values()): raise SystemExit(1)

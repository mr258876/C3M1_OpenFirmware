#!/usr/bin/env python3
"""v0.1.55 RGB-only PLL4 mainline contract; no target GPIO hardware emulation."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ov = (root / 'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_bytes()
cfg = (root / 'C3M1_Camera/Drivers/BSP/OV7725/ov7725cfg.h').read_bytes()
script = (root / 'build_clang.sh').read_text()
assert b'#define C3_RGB_PLL4_DEFAULT 1' in ov, 'RGB default must enable PLL4'
assert b'#if C3_TEST_PLL4 || (C3_RGB_PLL4_DEFAULT && !C3_TEST_PLL6 && \\' in ov
assert b'!C3_TEST_CLKDIV2 && !C3_TEST_PCLK_INV)' in ov
assert b'(void)ov7725_write_reg(COM4, 0x41u);' in ov
assert ov.index(b'(void)ov7725_write_reg(COM4, 0x41u);') > ov.index(b'for (i = 0; i < sizeof(ov7725_init_reg_tb1)')
assert ov.index(b'(void)ov7725_write_reg(COM4, 0x41u);') < ov.index(b'uint8_t ov7725_RAW_init(void)')
assert 'C3_RGB_PLL4_DEFAULT:-1' in script
assert '-DC3_RGB_PLL4_DEFAULT=${C3_RGB_PLL4_DEFAULT:-1}' in script
assert b'C3_SCCB_RESTART_VERIFY' in ov
assert b'C3_SCCB_FLOAT_READ' in (root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes()

raw = cfg.split(b'const uint8_t ov7725_init_reg_tbRAW[][2] =', 1)[1].split(
    b'const uint8_t ov7725_init_reg_tb1[][2] =', 1)[0]
rgb = cfg.split(b'const uint8_t ov7725_init_reg_tb1[][2] =', 1)[1]
assert b'{COM4,      0x41}' in raw and b'{CLKRC,     0x01}' in raw
assert b'{COM4,      0xc1}' in rgb and b'{CLKRC,     0x00}' in rgb
print('PASS v155: RGB PLL4 post-table default, strict rollback flag, unchanged independent RAW table')

#!/usr/bin/env python3
"""Source-level contract for v0.1.46 conservative SCCB timing sweep.
Actual voltage/timing can only be verified on hardware.
"""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes().decode('latin1')
m=(r/'C3M1_Camera/Core/Src/main.c').read_text()
d=(r/'C3M1_Camera/Core/Src/c3_sccb_diag.c').read_text()
b=(r/'build_clang.sh').read_text()
o=(r/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
assert 'volatile uint32_t c3_sccb_half_period_us = 50u;' in s
assert '#define C3_SCCB_MAIN_HALF_US 50u' in m
assert 'C3_SCCB_MAIN_HALF_US:-50' in b
assert '20|50|100|200' in b
assert 'static const uint16_t speed_us[3]={50u,100u,200u}' in d
assert 'speed_idx=(uint8_t)((speed_idx+1u)%3u)' in d
assert 'c3_sccb_half_period_us = half_us;' in d
assert 'draw_table(raw,half_us,code)' in d
assert 'char line[]="050us INIT 0"' in d
assert 'line[11]=hexdigit(init_code)' in d
assert 'ov7725_read_reg(i->reg)' in d and 'ov7725_read_reg_restart(i->reg)' in d
assert 'for (unsigned n=0;n<5u;++n)' in d
assert '++i->l_ff' in d and '++i->r_ff' in d
assert 'if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;' in o
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;' in o
assert 'C3_RAW_VERIFY_WARN_IF_FF:-0' in b
for v, txt in [(50,'050'), (100,'100'), (200,'200')]:
    assert f'{(v//100)%10}{(v//10)%10}{v%10}' == txt
print('v146 slow SCCB contract OK: 50us main / 50-100-200 scan / strict RAW')

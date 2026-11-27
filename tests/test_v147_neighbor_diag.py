#!/usr/bin/env python3
"""v0.1.47 SCCB neighbor-read diagnostic contracts (no hardware timing claim)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
d=(root/'C3M1_Camera/Core/Src/c3_sccb_diag.c').read_text()
o=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
s=(root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes().decode('latin1')
assert 'static const uint16_t speed_us' not in d
assert 'const uint16_t half_us=50u' in d
for reg in range(0x8E,0x9D):
    assert f'{{0x{reg:02X}u,0,0,0,0}}' in d
assert 'n->leg_last=ov7725_read_reg(n->reg);' in d
assert 'n->alt_last=ov7725_read_reg_restart(n->reg);' in d
assert 'n->leg_last==0xFFu' in d and 'n->alt_last==0xFFu' in d
assert 'scan_all();' in d and 'scan_neighbors();' in d
assert 'page^=1u' in d and 'raw^=1u' in d and 'refresh=1u' in d
assert 'ov7725_write_reg(' not in d, 'diagnostic must not have direct register writes'
assert 'if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;' in o
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;' in o
assert 'volatile uint32_t c3_sccb_half_period_us = 50u;' in s
print('v147 SCCB address-window diagnostics: PASS')

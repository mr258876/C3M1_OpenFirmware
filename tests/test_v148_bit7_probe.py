#!/usr/bin/env python3
"""v0.1.48 source contracts: separated passive and deliberate write/read tests.
No claim that host testing can prove an SCCB physical waveform.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'C3M1_Camera/Core/Src/c3_sccb_diag.c').read_text()
o=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
# The actual written/read values in the current OV7725 tables, incl. MSB transitions
for reg, value in [(0x84,0x7b),(0x85,0x86),(0x86,0x8e),(0x87,0x97),
    (0x88,0xa4),(0x89,0xaf),(0x8a,0xc5),(0x8b,0xd7),(0x8c,0xe8),
    (0x1c,0x7f),(0x1d,0xa2),(0x94,0xb0),(0x96,0x13),(0x99,0x91)]:
    assert f'{{0x{reg:02X}u,0x{value:02X}u,0,0,0,0}}' in s
# Both SCCB read protocols, five readings / register, no automatic writes on passive pages.
assert 'static void scan_msb(void)' in s
assert 'n->leg_last=ov7725_read_reg(n->reg);' in s
assert 'n->alt_last=ov7725_read_reg_restart(n->reg);' in s
assert 'if(n->leg_last==0xFFu)' in s
assert 'static void run_bit7_probe(uint8_t raw)' in s
for reg,value in [(0x96,0x13),(0x96,0x93),(0x94,0x30),(0x94,0xb0)]:
    assert f'{{0x{reg:02X}u,0x{value:02X}u,0,0,0,0,0,0}}' in s
assert 'n->l_last==n->value' in s and 'n->a_last==n->value' in s
# Deliberately restoring original values AND full sensor initialization
assert '(void)ov7725_write_reg(0x96u,0x13u);' in s
assert '(void)ov7725_write_reg(0x94u,0xB0u);' in s
assert 'raw ? ov7725_RAW_init() : ov7725_init()' in s
# Only manual shutter on page 3 invokes value-injection probe.
assert 'if (page==3u)' in s and 'run_bit7_probe(raw);' in s
assert 'page=(uint8_t)((page+1u)%5u);' in s
assert 'if (pressed & C3_BTN_4X) {raw^=1u;refresh=1u;break;}' in s
# Keep RAW critical format verification strict; no FF bypass.
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;' in o
assert 'if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;' in o
for forbidden in ('HAL_SD_WriteBlocks(', 'HAL_FLASH_Program(', 'f_write(', 'c3_dng_save('):
    assert forbidden not in s
print('PASS: v0.1.48 bit7 survey, deliberate write/read and recovery contracts')

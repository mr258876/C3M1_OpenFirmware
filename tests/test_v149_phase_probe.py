#!/usr/bin/env python3
"""v0.1.49 source/electrical safety checks; NOT live SCCB waveform verification."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
s=(root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes().decode('latin1')
h=(root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.h').read_bytes().decode('latin1')
d=(root/'C3M1_Camera/Core/Src/c3_sccb_diag.c').read_text()
o=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_bytes().decode('latin1')
assert 'void sccb_trace_read_reg(' in s and 'void sccb_trace_read_reg(' in h
part = s[s.index('void sccb_trace_read_reg('):]
for x in ('sccb_send_byte((uint8_t)(device_addr & 0xFEu))',
          'sccb_send_byte(reg)', 'sccb_stop();','sccb_start();',
          'sccb_send_byte((uint8_t)(device_addr | 0x01u))',
          'SCCB_SDA(1)', 'GPIOG->CRH','(0x4u << 20)',
          'pg13_saved','SCCB_SCL(1)', 'SCCB_SCL(0)',
          'sccb_nack();', 'sccb_stop();'):
    assert x in part, x
assert 'i < 8u' in part and part.count('GPIOG->IDR & GPIO_PIN_13') == 3
loop=part[part.index('for (unsigned i = 0u; i < 8u; ++i)'):]
assert loop.index('trace->low = (uint8_t)((trace->low << 1)') < loop.index('SCCB_SCL(1)') < loop.index('trace->early = (uint8_t)((trace->early << 1)') < loop.index('trace->late = (uint8_t)((trace->late << 1)') < loop.index('SCCB_SCL(0)')
assert 'GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | pg13_saved;' in part
assert 'SCCB_SDA(1)' in part[:part.index('GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | (0x4u << 20)')]
assert 'phase_ready=0u' in d and 'phase_ready=1u' in d
for r,v in [(0x1d,0xa2),(0x96,0x13),(0x96,0x93),(0x94,0x30),(0x94,0xb0)]:
    assert f'{{0x{r:02X}u,0x{v:02X}u,{{0,0,0}},{{0,0,0}}}}' in d
assert 'if (p->reg!=0x1Du)' in d
assert 'if (page==4u)' in d and 'run_phase_probe(raw);' in d
assert 'page=(uint8_t)((page+1u)%5u);' in d
assert 'if (page==3u)' in d and 'run_bit7_probe(raw);' in d
assert '(void)ov7725_write_reg(0x96u,0x13u)' in d
assert '(void)ov7725_write_reg(0x94u,0xB0u)' in d
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u' in o
for f in ('HAL_SD_WriteBlocks(', 'HAL_FLASH_Program(', 'f_write(', 'c3_dng_save('):
    assert f not in d
print('PASS: v0.1.49 8-bit triple sampler, PG13 float/restore, user-trigger, sensor restore, RAW strict')

#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
cam=(root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
ov=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_bytes().decode('latin1')
bld=(root/'build_clang.sh').read_text()
assert 'DNG SENSOR INIT ERR' in main and 'BMP SENSOR INIT ERR' in main
assert 'EXPOSURE APPLY ERR' in main and 'REG 00 EXP 00 GOT 00' in main
assert 'c3_camera_last_still_code = code;' in cam
assert 'c3_camera_last_still_stage = 2u;' in cam
assert '#define C3_RAW_VERIFY_WARN_IF_FF 0' in ov
raw=ov[ov.index('uint8_t ov7725_RAW_init(void)'):ov.index('uint8_t ov7725_preview_window_set(')]
assert '#if C3_RAW_VERIFY_WARN_IF_FF' in raw
assert 'if (!rgb_verify_or_warn_if_ff(MTX1' in raw
assert 'if (!verify_static_reg(MTX1' in raw
assert 'if (!verify_static_reg(DSP_Ctrl4' in raw
assert 'c3_ov_verify_warn_mask = 0u;' in raw
assert 'C3_RAW_VERIFY_WARN_IF_FF:-0' in bld
assert 'C3_CAM_COM3_RGB:-0xD0' in bld
assert 'C3_LCD_DEFAULT_INV_ON' in bld
print('PASS: still error shows stage/code/reg, RAW default strict, optional FF-only gate, color defaults preserved')

#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=(root/'C3M1_Camera/Core/Src/main.c').read_text()
o=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
b=(root/'build_clang.sh').read_text()
d=(root/'C3M1_Camera/Core/Src/c3_dng.c').read_text()
assert m.count('c3_dng_capture_preview_awb(ov7725_read_reg_checked)')==2
assert m.index('c3_dng_capture_preview_awb') < m.index('c3_camera_raw_init()')
assert '#define C3_RAW_CLKRC 0' in m and '#define C3_RAW_CLKRC 0u' in o
assert "'C3_RAW_CLKRC:0'" in b
assert 'C3_DNG_DYNAMIC_WB' in b and 'C3_DNG_EXPERIMENTAL_MATRIX' in b
assert '50964' in d and 'captured_neutral' in d
print('PASS v164 preview AWB before sensor RAW initialization, default CLKRC0, optional profile and WB')

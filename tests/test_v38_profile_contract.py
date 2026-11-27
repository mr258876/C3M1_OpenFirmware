"""Source contract checks, no board emulation or fabricated cycle results."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
core = root/'C3M1_Camera/Core'
cam = (core/'Src/c3_camera.c').read_text()
hud = (core/'Src/c3_osd.c').read_text()
script = (root/'build_clang.sh').read_text()
lcd = (core/'Inc/c3_lcd_dma.h').read_text()
fsmc = (core/'Src/fsmc.c').read_text()
assert '#define C3_PREVIEW_ROW_OPT 2' in cam
assert '#define C3_LCD_DMA_ENABLE 1' in lcd
assert '#define C3_FSMC_DATAST 4' in fsmc
assert 'C3_PREVIEW_ROW_OPT:-2' in script
assert 'C3_LCD_DMA_ENABLE:-1' in script
assert 'C3_FSMC_DATAST:-4' in script
assert '#if C3_ROW_DETAIL_PROFILE == 2' in cam
assert 'c3_row_profile_mix_osd(line, c3_detail_fifo_row, mask);' in cam
assert '#if C3_ROW_DETAIL_PROFILE == 1' in cam
assert 'c3_row_copy_osd(line, mask);' in cam
assert 'c3_camera_fifo_read_fast()' in cam
for char_str, positions in (
    ('PLN 000.0 OSD 000.0', (4,14)),
    ('EXTRA 000.0 ROW 000.0', (6,16)),
    ('READ 000.0 MIX 000.0', (5,15)),
    ('ROW 000.0 DRAW 000.0', (4,15)),
):
    assert char_str in hud
    assert len(char_str) <= 28
    for pos in positions:
        assert char_str[pos:pos+5] == '000.0', (char_str,pos)
assert 'C3_ROW_DETAIL_PROFILE requires StageHUD' in script
print('PASS: RowOpt2/LCD DMA/FSMC D4 defaults, read-only instrumentation and HUD offsets')

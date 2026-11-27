#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
osd=(root/'C3M1_Camera/Core/Src/c3_osd.c').read_text()
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
cam=(root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
assert 'text(6, 5, "C3M1", 2);' not in osd
assert 'text(8, 5, mode_names[shoot_mode], 2);' in osd
assert 'text(145, 5, fps, 2);' in osd
assert '"GR"' not in osd
assert 'battery_icon();' in osd
assert 'text(220, 5, dma_disabled' not in osd
assert 'c3_osd_battery' not in cam  # no ADC/pixel overhead inside preview draw
assert 'c3_battery_read_mv' not in cam
start=main.index('if (c3_camera_preview_acquire(800u))')
read=main.index('c3_battery_read_mv(&mv)',start)
release=main.index('c3_camera_preview_release();',start)
assert release < read
assert main.index('if (nav.page != C3_UI_PREVIEW)') < start
assert main.index('if (!camera_ok)',main.index('if (nav.page != C3_UI_PREVIEW)')) < start
assert '(uint32_t)(now - osd_battery_tick) >= 5000u' in main
assert 'battery_refresh_tick' in main and 'c3_menu_draw_settings' in main
print('PASS v159: top-band layout, ADC outside image path, menu battery preserved')

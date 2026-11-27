#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
F=(-std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_LCD_DMA_ENABLE=0 -DC3_4X_FAST_8000=1 -DC3_RGB_EXHCL=0x80)
for timing in 0 1; do
 for lock in 0 1; do
  if [[ "$timing" == 0 && "$lock" == 1 ]]; then continue; fi # zoom lock requires dynamic timing
  echo "OSD timing=$timing zoom_lock=$lock"
  cc "${F[@]}" -DC3_SHUTTER_DYNAMIC_TIMING=$timing -DC3_ZOOM_AE_LOCK=$lock tests/test_osd.c "$P"/Src/{c3_osd,c3_font,c3_exposure,c3_meter,c3_shutter_timing}.c -o "$O/osd"
  "$O/osd"
 done
done
python3 tests/test_v159_osd_battery_contract.py

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
F=(-std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc")
cc "${F[@]}" tests/test_ui_nav.c "$P"/Src/{c3_ui_nav,c3_settings}.c -o "$O/nav"
"$O/nav"
cc "${F[@]}" -Itests/flash_mock tests/test_flash_migration.c "$P"/Src/{c3_settings,c3_settings_flash}.c -o "$O/flash"
"$O/flash"
cc "${F[@]}" -DC3_SLEEP_HOST_TEST tests/test_v160_sleep.c "$P"/Src/c3_sleep.c -o "$O/sleep"
"$O/sleep"
for timing in 0 1; do
  for test in test_osd test_v184_card_warning; do
    cc "${F[@]}" -DC3_LCD_DMA_ENABLE=0 -DC3_SHUTTER_DYNAMIC_TIMING=$timing -DC3_4X_FAST_8000=1 -DC3_RGB_EXHCL=0x80 tests/$test.c "$P"/Src/{c3_osd,c3_font,c3_exposure,c3_meter,c3_shutter_timing}.c -o "$O/$test"
    "$O/$test"
  done
done

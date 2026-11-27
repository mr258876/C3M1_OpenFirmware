#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
P=C3M1_Camera/Core
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
D="${1:-../logs/previews}";mkdir -p "$D"
F=(-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie -I"$P/Inc" -I"$P/Src" -DC3_FUNCTION_MENU=${C3_FUNCTION_MENU:-0} -DC3_MENU_ACTIONS=${C3_MENU_ACTIONS:-0} -DC3_I18N=1 -DC3_UI_ICONS=${C3_UI_ICONS:-0} -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_PLAYBACK_ENABLE=1 -DC3_OSD_SHUTTER_TIME=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1 -DC3_FOCUS_GREEN=1 -DC3_PEAK_EXPERIMENT=4)
cc "${F[@]}" tests/test_v210_i18n.c "$P"/Src/{c3_i18n,c3_font,c3_menu,c3_settings,c3_ui_nav}.c -o "$O/i18n"
"$O/i18n" "$D"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -Itests/flash_mock -DC3_FUNCTION_MENU=${C3_FUNCTION_MENU:-0} -DC3_MENU_ACTIONS=${C3_MENU_ACTIONS:-0} -DC3_I18N=1 -DC3_UI_ICONS=${C3_UI_ICONS:-0} -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_FOCUS_GREEN=1 tests/test_flash_migration.c "$P"/Src/{c3_settings,c3_settings_flash}.c -o "$O/flash"
"$O/flash"
cc "${F[@]}" -Itests/playback_mock -DC3_LCD_DMA_ENABLE=1 tests/test_v207_playback.c "$P"/Src/{c3_i18n,c3_font,c3_playback,c3_play_decode,c3_dng,c3_bmp,c3_ui_nav,c3_settings}.c -o "$O/play"
"$O/play"

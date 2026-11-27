#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
P=C3M1_Camera/Core
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
D="${1:-../previews}";mkdir -p "$D"
F=(-ffunction-sections -fdata-sections -Wl,--gc-sections -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie -I"$P/Inc" -I"$P/Src" -DC3_POSTSHOT=1 -DC3_POSTSHOT_IMMEDIATE=1 -DC3_POSTSHOT_KEEP_BG=1 -DC3_DEFAULTS_V218=1 -DC3_LANGUAGE_LAYOUT=1 -DC3_LANGUAGE_SETUP=1 -DC3_STARTUP_HINT=1 -DC3_FUNCTION_MENU=1 -DC3_MENU_ACTIONS=1 -DC3_I18N=1 -DC3_UI_ICONS=1 -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_PLAYBACK_ENABLE=1 -DC3_OSD_SHUTTER_TIME=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1 -DC3_FOCUS_GREEN=1 -DC3_PEAK_EXPERIMENT=4)
cc "${F[@]}" tests/test_v215_ui.c "$P"/Src/{c3_i18n,c3_font,c3_menu,c3_settings,c3_ui_nav}.c -o "$O/i18n"
"$O/i18n" "$D"
cc "${F[@]}" tests/test_v216_nav.c "$P"/Src/{c3_ui_nav,c3_settings}.c -o "$O/nav"
"$O/nav"
cc "${F[@]}" -Itests/flash_mock tests/test_flash_migration.c "$P"/Src/{c3_settings,c3_settings_flash}.c -o "$O/flash"
"$O/flash"
cc "${F[@]}" -Itests/playback_mock -DC3_LCD_DMA_ENABLE=1 tests/test_v207_playback.c "$P"/Src/{c3_i18n,c3_font,c3_playback,c3_play_decode,c3_dng,c3_bmp,c3_ui_nav,c3_settings}.c -o "$O/play"
"$O/play"

cc "${F[@]}" -DC3_SLEEP_HOST_TEST tests/test_v218_defaults.c "$P"/Src/{c3_settings,c3_sleep}.c -o "$O/defaults"
"$O/defaults"
python3 tests/test_v218_hint_contract.py
cc "${F[@]}" -DC3_INPUT_HOST_TEST tests/test_v220_postshot.c "$P"/Src/{c3_settings,c3_buttons,c3_input,c3_ui_nav}.c -o "$O/postshot"
"$O/postshot"
python3 - "$O/v221_modal.inc" <<'PY'
from pathlib import Path
import sys,re
s=Path('C3M1_Camera/Core/Src/main.c').read_text()
a=s.index('#if C3_POSTSHOT_KEEP_BG\n/* The previous RGB preview');b=s.index('\n#endif\n\nstatic const char *save_error_text',a)
Path(sys.argv[1]).write_text(s[a:b])
# Every successful transaction uses the modal only after commit & FIFO release.
assert len(re.findall(r'c3_recovery_save_finished\(&recovery, result\);\s*sd_ok = \(uint8_t\)\(result == C3_SAVE_OK\);\s*if \(result == C3_SAVE_OK\) \{\s*show_save_success\(\);\s*c3_camera_raw_discard\(\);\s*#if C3_POSTSHOT\s*(?:#if C3_POSTSHOT_KEEP_BG\s*postshot_prepare_retry_background\(&settings\);\s*#endif\s*)?postshot_used=postshot_wait',s))==3
assert s.count('if(postshot_used)postshot_resume_input(')==3
assert s.count('postshot_prepare_retry_background(&settings);')==1
print('PASS all three save-success branches gated after transaction commit and FIFO discard; resume consumes modal input')
PY
cc "${F[@]}" -I"$O" -DC3_INPUT_HOST_TEST tests/test_v221_modal.c "$P"/Src/{c3_settings,c3_buttons,c3_input,c3_ui_nav,c3_i18n,c3_font}.c -o "$O/modal"
"$O/modal"

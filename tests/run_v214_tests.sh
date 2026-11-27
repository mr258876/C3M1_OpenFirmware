#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_FUNCTION_MENU=1 -DC3_MENU_ACTIONS=1 -DC3_I18N=1 -DC3_UI_ICONS=1 -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_PLAYBACK_ENABLE=1 -DC3_FOCUS_GREEN=1 tests/test_v214_nav.c "$P"/Src/{c3_ui_nav,c3_settings}.c -o "$O/nav"
"$O/nav"
C3_FUNCTION_MENU=1 C3_MENU_ACTIONS=1 C3_UI_ICONS=1 bash tests/run_v210_tests.sh "${1:-../previews}"
C3_FUNCTION_MENU=1 bash tests/run_v213_tests.sh

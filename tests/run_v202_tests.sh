#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
F=(-std=c11 -O2 -Wall -Wextra -Werror -DC3_PEAK_FULL_SP_ONLY=1 -DC3_PEAK_EXPERIMENT=4 -DC3_FOCUS_GREEN=1 -DC3_PEAK_SP_CENTER_W=256 -I"$P/Inc")
cc "${F[@]}" -DEXPECT_SP_W=256 tests/test_v200_roi.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/roi"
"$O/roi"
cc "${F[@]}" tests/test_v202_menu.c "$P"/Src/{c3_focus,c3_settings,c3_ui_nav}.c -o "$O/menu"
"$O/menu"

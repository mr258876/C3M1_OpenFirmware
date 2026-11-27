#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for green in 0 1; do
 cc -std=c11 -O2 -Wall -Wextra -Werror -DC3_PEAK_EXPERIMENT=4 -DC3_FOCUS_GREEN=$green -I"$P/Inc" tests/test_v199_green.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/green"
 "$O/green"
done
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function -DC3_LIVE_PROFILE=1 -DC3_LCD_DMA_ENABLE=0 -DC3_SHUTTER_DYNAMIC_TIMING=1 -DC3_RGB_EXHCL=0x80 -I"$P/Inc" tests/test_v199_profile.c "$P"/Src/{c3_meter,c3_font,c3_exposure,c3_shutter_timing}.c -o "$O/profile"
"$O/profile"
# Run after all four build variants have been produced.
if [[ -f build_V199_REF_ACR8_F2/c3m1.elf && -f build_V199_GREEN_PROFILE_ACR8_F2/c3m1.elf && -f build_V199_PROFILE_ACR8_F2/c3m1.elf && -f build_V199_GREEN_ACR8_F2/c3m1.elf ]]; then
 python3 tests/test_v199_contract.py
fi

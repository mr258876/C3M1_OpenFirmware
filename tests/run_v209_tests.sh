#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
P=C3M1_Camera/Core
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
flags=(-std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_RGB_EXHCL=0x80 -DC3_SHUTTER_DYNAMIC_TIMING=1 -DC3_4X_FAST_8000=1 -DC3_ZOOM_AE_LOCK=1 -DC3_ZOOM_LOCK_DIAG=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1)
for t in test_v208_zoom_diag test_v209_zoom_sync;do
 cc "${flags[@]}" tests/$t.c "$P"/Src/{c3_exposure,c3_shutter_timing,c3_dng}.c -o "$O/$t"; "$O/$t"
done
python3 tests/test_v209_wait.py

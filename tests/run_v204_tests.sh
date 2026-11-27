#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for fast in 0 1;do
 cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_RGB_EXHCL=0x80 -DC3_SHUTTER_DYNAMIC_TIMING=1 -DC3_4X_FAST_8000=$fast -DC3_ZOOM_AE_LOCK=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1 tests/test_v204_zoom_lock.c "$P"/Src/{c3_exposure,c3_shutter_timing,c3_dng}.c -o "$O/zoom"
 "$O/zoom"
done

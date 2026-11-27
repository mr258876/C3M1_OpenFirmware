#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
W=$(mktemp -d);trap 'rm -rf "$W"' EXIT
for m in 0 1 2; do
  CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=$m -IC3M1_Camera/Core/Inc)
  cc "${CFLAGS[@]}" tests/test_v179_black.c C3M1_Camera/Core/Src/c3_dng.c -o "$W/meta$m"
  "$W/meta$m" "$W/meta$m.bin"
  cc "${CFLAGS[@]}" -DC3_STORAGE_HOST_TEST -Itests tests/test_storage_retry.c C3M1_Camera/Core/Src/{c3_storage,c3_dng,c3_bmp,c3_capture_recovery}.c -o "$W/retry$m"
  "$W/retry$m" "$W/retry$m.bmp" "$W/retry$m.dng"
done
python3 tests/test_v182_acr.py "$W" "$@"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
OUT="$(mktemp -d)";trap 'rm -rf "$OUT"' EXIT
CFLAGS=( -std=c11 -O2 -Wall -Wextra -Werror -DC3_DNG_SOFT_PROFILE=1 -I"$ROOT/C3M1_Camera/Core/Inc" )
for mode in 0 1; do
    cc "${CFLAGS[@]}" -DC3_DNG_BLACK_CALIBRATION=$mode tests/test_v179_black.c C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/dng_$mode"
    "$OUT/dng_$mode" "$OUT/$mode.bin"
done
python3 tests/test_v179_black.py "$OUT/1.bin" "$OUT/0.bin" "${C3_CALIBRATION_CSV:-$ROOT/tools/v179_calibration_45.csv}" "$@"
cc "${CFLAGS[@]}" -DC3_DNG_BLACK_CALIBRATION=1 -I"$ROOT/tests" -DC3_STORAGE_HOST_TEST \
   tests/test_storage_retry.c C3M1_Camera/Core/Src/c3_storage.c \
   C3M1_Camera/Core/Src/c3_dng.c C3M1_Camera/Core/Src/c3_bmp.c \
   C3M1_Camera/Core/Src/c3_capture_recovery.c -o "$OUT/retry"
"$OUT/retry"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cd "$ROOT"
CFLAGS=( -std=c11 -O2 -Wall -Wextra -Werror -DC3_DNG_SOFT_PROFILE=1 -I"$ROOT/C3M1_Camera/Core/Inc" )
cc "${CFLAGS[@]}" tests/test_dng.c C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/test_dng"
"$OUT/test_dng" "$OUT/generated.DNG"
# Portable frozen vectors; additionally compare to original DNG when supplied.
if [[ -n "${C3_M675_SOFT_REFERENCE:-}" ]]; then
  python3 tests/test_v173_dng_profile.py "$OUT/generated.DNG" "$C3_M675_SOFT_REFERENCE"
else
  python3 tests/test_v173_dng_profile.py "$OUT/generated.DNG"
fi
cc "${CFLAGS[@]}" -I"$ROOT/tests" -DC3_STORAGE_HOST_TEST \
   tests/test_storage_retry.c C3M1_Camera/Core/Src/c3_storage.c \
   C3M1_Camera/Core/Src/c3_dng.c C3M1_Camera/Core/Src/c3_bmp.c \
   C3M1_Camera/Core/Src/c3_capture_recovery.c -o "$OUT/test_retry"
"$OUT/test_retry"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for timing in 0 1; do
 cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_LCD_DMA_ENABLE=0 -DC3_SHUTTER_DYNAMIC_TIMING=$timing -DC3_RGB_EXHCL=0x80 tests/test_v186_exposure_warning.c "$P"/Src/{c3_meter,c3_osd,c3_font,c3_exposure,c3_shutter_timing}.c -o "$O/warn"
 "$O/warn"
done

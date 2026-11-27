#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for pack in 0 1; do
 echo "Host C reference: PACK16=$pack"
 cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_ROW_PACK16="$pack" -DC3_PEAK_EXPERIMENT=4 tests/test_v192_peak.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/peak"
 "$O/peak"
 cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_ROW_PACK16="$pack" tests/test_row_opt37.c -o "$O/rows"
 "$O/rows"
done
python3 tests/test_v193_fifo_arm.py

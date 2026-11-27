#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for w in 160 224 256; do
 cc -std=c11 -O2 -Wall -Wextra -Werror -DC3_PEAK_EXPERIMENT=4 -DC3_FOCUS_GREEN=1 -DC3_PEAK_SP_CENTER_W=$w -DEXPECT_SP_W=$w -I"$P/Inc" tests/test_v200_roi.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/roi"
 "$O/roi"
done

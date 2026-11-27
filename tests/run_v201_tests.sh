#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
cc -std=c11 -O2 -Wall -Wextra -Werror -DC3_DMA_UNPACK=3 -DC3_PEAK_EXPERIMENT=4 -DC3_FOCUS_GREEN=1 -DC3_PEAK_SP_CENTER_W=256 -I"$P/Inc" tests/test_v201_unpack.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/unpack"
"$O/unpack"

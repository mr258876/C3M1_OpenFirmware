#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
cc -std=c11 -O2 -Wall -Wextra -Werror -DC3_PEAK_EXPERIMENT=4 -I"$P/Inc" tests/test_v198_live.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/live"
"$O/live"
python3 tests/test_v198_contract.py

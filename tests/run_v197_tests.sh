#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -DC3_PEAK_EXPERIMENT=4 -IC3M1_Camera/Core/Inc tests/test_v197_load.c -o "$O/load"
"$O/load"
cc -std=c11 -O2 -Wall -Wextra -Werror -IC3M1_Camera/Core/Inc tests/test_v196_high.c -o "$O/high"
"$O/high"
cc -std=c11 -O2 -Wall -Wextra -Werror -IC3M1_Camera/Core/Inc tests/test_v195_margin.c -o "$O/margin"
"$O/margin"
python3 tests/test_v197_contract.py

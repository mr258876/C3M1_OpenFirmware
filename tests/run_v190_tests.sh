#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
bash tests/run_v189_tests.sh
for pack in 0 1; do
  cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_ROW_PACK16="$pack" tests/test_v190_fast_score.c "$P/Src/c3_focus.c" -o "$O/fast"
  "$O/fast"
done

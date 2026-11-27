#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -IC3M1_Camera/Core/Inc tests/test_v195_margin.c -o "$O/plan"
"$O/plan"
python3 tests/test_v195_contract.py

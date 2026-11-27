#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" tests/test_v189_score.c "$P"/Src/{c3_focus,c3_settings}.c -o "$O/score"
"$O/score"

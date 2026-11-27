#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
for config in '0 0' '1 0' '1 1'; do
  read -r timing fast <<< "$config"
  cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" -DC3_RGB_EXHCL=0x80 -DC3_SHUTTER_DYNAMIC_TIMING="$timing" -DC3_4X_FAST_8000="$fast" tests/test_v188_night_exit.c "$P"/Src/{c3_exposure,c3_shutter_timing}.c -o "$O/night"
  "$O/night"
done

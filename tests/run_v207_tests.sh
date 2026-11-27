#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d);trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
cc -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie -Itests/playback_mock -I"$P/Inc" -DC3_PLAYBACK_ENABLE=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1 -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_FOCUS_GREEN=1 -DC3_PEAK_EXPERIMENT=4 tests/test_v207_playback.c "$P"/Src/{c3_playback,c3_play_decode,c3_dng,c3_bmp,c3_ui_nav,c3_settings}.c -o "$O/play"
"$O/play"
cc -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie -I"$P/Inc" -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1 tests/test_v207_fuzz.c "$P"/Src/{c3_play_decode,c3_dng}.c -o "$O/fuzz"
"$O/fuzz"
python3 tests/test_v207_decode.py "$@"

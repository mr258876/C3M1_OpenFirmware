#!/usr/bin/env bash
# v179: v178 BLC ON + measured four-channel DNG black metadata.
set -euo pipefail
cd "$(dirname "$0")"
C3_DNG_BLACK_CALIBRATION="${C3_DNG_BLACK_CALIBRATION:-1}" \
C3_RAW_BLC_ENABLE=1 C3_DNG_SETTLE_LOCKED=6 bash build_v178.sh

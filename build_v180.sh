#!/usr/bin/env bash
# v180: v179 calibration unchanged; explicit VSYNC-only stabilization A/B.
set -euo pipefail
cd "$(dirname "$0")"
C3_DNG_VSYNC_SETTLE=1 \
C3_DNG_FAST_SETTLE_FRAMES="${C3_DNG_FAST_SETTLE_FRAMES:-6}" \
  bash build_v179.sh

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
tag=V187_ACR8_F2
if [[ "${C3_METER_DIAG:-0}" == 1 ]]; then tag="${tag}_DIAG"; fi
C3_DNG_ACR_MODE=1 C3_DNG_FAST_SETTLE_FRAMES=2 C3_4X_FAST_8000=1 \
C3_BUILD_TAG="$tag" bash build_v182.sh

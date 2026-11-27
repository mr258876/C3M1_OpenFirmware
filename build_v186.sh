#!/usr/bin/env bash
# v0.1.86 validated mainline selection: ACR8 / F2 / OPT.
set -euo pipefail
cd "$(dirname "$0")"
C3_DNG_ACR_MODE=1 C3_DNG_FAST_SETTLE_FRAMES=2 C3_4X_FAST_8000=1 \
C3_BUILD_TAG=V186_ACR8_F2 bash build_v182.sh

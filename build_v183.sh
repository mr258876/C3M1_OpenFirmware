#!/usr/bin/env bash
# v0.1.83: four-column preview OSD; same ACR mode and settle options as v182.
set -euo pipefail
cd "$(dirname "$0")"
mode="${C3_DNG_ACR_MODE:-2}"
frames="${C3_DNG_FAST_SETTLE_FRAMES:-2}"
C3_BUILD_TAG="${C3_BUILD_TAG:-OSD_ACR${mode}_F${frames}}" \
 C3_DNG_ACR_MODE="$mode" C3_DNG_FAST_SETTLE_FRAMES="$frames" bash build_v182.sh

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_BUILD_TAG="${C3_BUILD_TAG:-V211_BITMAP_ACR8_F2}"
bash build_v210.sh

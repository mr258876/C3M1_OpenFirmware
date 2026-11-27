#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
case "${1:-SP24}" in
  SP22) exp=1; prof=0;;
  SP24) exp=2; prof=0;;
  FAST) exp=3; prof=0;;
  PROFILE) exp=2; prof=1;;
  *) echo 'usage: build_v192.sh SP22|SP24|FAST|PROFILE' >&2; exit 2;;
esac
C3_PEAK_EXPERIMENT=$exp C3_PEAK_PROFILE=$prof C3_PREVIEW_PROFILE=$prof \
 C3_DNG_ACR_MODE=1 C3_DNG_FAST_SETTLE_FRAMES=2 C3_4X_FAST_8000=1 \
 C3_BUILD_TAG="V192_${1:-SP24}_ACR8_F2" bash build_v182.sh

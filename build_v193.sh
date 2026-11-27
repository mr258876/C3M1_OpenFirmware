#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
variant=${1:-ASM8}
case "$variant" in
  REF) arm=0; prof=0;;
  PACK16) arm=1; prof=0;;
  ASM8) arm=2; prof=0;;
  PROFILE_REF) arm=0; prof=1;;
  PROFILE_ASM8) arm=2; prof=1;;
  *) echo 'usage: build_v193.sh REF|PACK16|ASM8|PROFILE_REF|PROFILE_ASM8' >&2; exit 2;;
esac
C3_PEAK_EXPERIMENT=4 C3_PEAK_PROFILE=$prof C3_PREVIEW_PROFILE=$prof \
 C3_ROW_PACK16=0 C3_FIFO_ROW_ARM=$arm C3_FIFO_ASM_MODE=0 C3_FIFO_CPU_BURST_BYTES=0 \
 C3_DNG_ACR_MODE=1 C3_DNG_FAST_SETTLE_FRAMES=2 C3_4X_FAST_8000=1 \
 C3_BUILD_TAG="V193_${variant}_ACR8_F2" bash build_v182.sh

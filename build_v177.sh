#!/usr/bin/env bash
# v177: validated v173 M675 Soft mainline + 1X/4X/RAW shutter timing optimization.
set -euo pipefail
cd "$(dirname "$0")"
C3_SHUTTER_DYNAMIC_TIMING=1 \
C3_4X_FAST_8000="${C3_4X_FAST_8000:-1}" \
C3_DNG_SOFT_PROFILE=1 \
C3_RGB_EXHCL=0x80 \
C3_SDIO_TX_DMA=1 \
C3_FAST_RAW_SWITCH=1 \
C3_FAST_RGB_RESTORE=1 \
C3_RAW_CLKRC=0 \
C3_RAW_FREEZE_AWB=1 \
C3_OSD_SHUTTER_TIME=1 \
C3_NO_SAVED=1 \
  bash build_clang.sh

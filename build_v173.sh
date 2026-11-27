#!/usr/bin/env bash
# C3M1 v0.1.73 M675 Soft Camera Raw profile; EXHCL80+DMA+AWB freeze.
set -euo pipefail
cd "$(dirname "$0")"
C3_SHUTTER_DYNAMIC_TIMING=0 \
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

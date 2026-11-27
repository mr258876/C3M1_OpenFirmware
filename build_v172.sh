#!/usr/bin/env bash
# Reproducible C3M1 v0.1.72 mainline (EXHCL80, SDIO DMA, RAW no-reset round trip).
set -euo pipefail
cd "$(dirname "$0")"
C3_RGB_EXHCL=0x80 \
C3_SDIO_TX_DMA=1 \
C3_FAST_RAW_SWITCH=1 \
C3_FAST_RGB_RESTORE=1 \
C3_RAW_CLKRC=0 \
C3_RAW_FREEZE_AWB=1 \
C3_OSD_SHUTTER_TIME=1 \
C3_NO_SAVED=1 \
  bash build_clang.sh

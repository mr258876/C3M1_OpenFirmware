#!/usr/bin/env bash
# H (mode 2) is the precision-preserving default; E (mode 1) retains RAW8 size.
set -euo pipefail
cd "$(dirname "$0")"
mode="${C3_DNG_ACR_MODE:-2}"
case "$mode" in 1|2) ;; *) echo 'select E=1 or H=2' >&2; exit 2;; esac
frames="${C3_DNG_FAST_SETTLE_FRAMES:-2}"
C3_DNG_ACR_MODE="$mode" C3_DNG_FAST_SETTLE_FRAMES="$frames" \
 C3_BUILD_TAG="${C3_BUILD_TAG:-ACR${mode}_F${frames}}" bash build_v180.sh

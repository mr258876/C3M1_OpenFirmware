#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_I18N=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V210_I18N_ACR8_F2}"
bash build_v209.sh "${1:-SYNC}"

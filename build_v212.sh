#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_UI_ICONS="${C3_UI_ICONS:-1}"
export C3_BUILD_TAG="${C3_BUILD_TAG:-V212_ICONS_ACR8_F2}"
bash build_v211.sh

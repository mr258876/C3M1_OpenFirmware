#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_MENU_ACTIONS=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V213_MENU_ACR8_F2}"
bash build_v212.sh

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_STARTUP_HINT=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V215_HINT_ACR8_F2}"
bash build_v214.sh

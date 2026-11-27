#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_POSTSHOT_KEEP_BG=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V221_POSTSHOT_ACR8_F2}"
bash build_v220.sh

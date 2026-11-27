#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_POSTSHOT=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V219_POSTSHOT_ACR8_F2}"
bash build_v218.sh

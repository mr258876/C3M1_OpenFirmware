#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_POSTSHOT_IMMEDIATE=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V220_POSTSHOT_ACR8_F2}"
bash build_v219.sh

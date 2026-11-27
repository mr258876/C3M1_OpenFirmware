#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_DEFAULTS_V218=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V218_DEFAULTS_ACR8_F2}"
bash build_v217.sh

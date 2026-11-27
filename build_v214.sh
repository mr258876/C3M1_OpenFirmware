#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_FUNCTION_MENU=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V214_FUNCTION_ACR8_F2}"
bash build_v213.sh

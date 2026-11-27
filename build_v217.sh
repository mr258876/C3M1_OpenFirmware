#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_LANGUAGE_LAYOUT=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V217_LANGUAGE_UI_ACR8_F2}"
bash build_v216.sh

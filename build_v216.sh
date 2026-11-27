#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export C3_LANGUAGE_SETUP=1
export C3_BUILD_TAG="${C3_BUILD_TAG:-V216_LANGUAGE_ACR8_F2}"
bash build_v215.sh

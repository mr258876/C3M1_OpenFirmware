#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec bash build_v221.sh "$@"

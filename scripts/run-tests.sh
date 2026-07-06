#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../build"
ctest --output-on-failure -j"$(nproc)"

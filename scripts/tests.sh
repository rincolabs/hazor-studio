#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../build"
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j"$(nproc)"
echo "✔ Tests built"

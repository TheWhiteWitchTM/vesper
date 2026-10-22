#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/fetch-vlc-plugins.sh"
mkdir -p "$ROOT/build"
cd "$ROOT/build"
qmake6 "$ROOT/vesper.pro"
make -j"$(nproc)"
echo "Built $ROOT/build/vesper"

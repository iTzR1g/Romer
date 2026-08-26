#!/usr/bin/env bash
set -euo pipefail
# Requires: cmake clang libjpeg sdl2 sdl2_ttf
#   brew install cmake jpeg sdl2 sdl2_ttf
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

echo "==> Configuring …"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building …"
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

echo "==> Done: ${BUILD_DIR}/romer"
ls -lh "$BUILD_DIR"/romer

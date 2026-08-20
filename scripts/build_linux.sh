#!/usr/bin/env bash
set -euo pipefail
# Requires: cmake g++ libjpeg-dev libx11-dev libxext-dev libsdl2-dev libsdl2-ttf-dev
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

echo "==> Configuring …"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building …"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "==> Done: ${BUILD_DIR}/romer_sm"
ls -lh "$BUILD_DIR"/romer_sm

#!/bin/bash
# Build MeshFixCore WASM
# Usage: bash build.sh

EMSDK_DIR="/c/emsdk"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Source emsdk environment
export EMSDK="$EMSDK_DIR"
export PATH="$EMSDK_DIR:$EMSDK_DIR/upstream/emscripten:$EMSDK_DIR/python/3.13.3_64bit:$EMSDK_DIR/node/22.16.0_64bit/bin:$PATH"
export EM_CONFIG="$EMSDK_DIR/.emscripten"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with Emscripten CMake toolchain
python "$EMSDK_DIR/upstream/emscripten/emcmake.py" cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    2>&1

# Build
python "$EMSDK_DIR/upstream/emscripten/emmake.py" cmake --build . -- -j4 2>&1

if [ -f "$BUILD_DIR/mesh-fix-core.js" ] && [ -f "$BUILD_DIR/mesh-fix-core.wasm" ]; then
    echo ""
    echo "=== BUILD SUCCESS ==="
    ls -la "$BUILD_DIR/mesh-fix-core.js" "$BUILD_DIR/mesh-fix-core.wasm"
else
    echo ""
    echo "=== BUILD FAILED ==="
    exit 1
fi

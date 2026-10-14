#!/usr/bin/env bash
# Compile the C++ core to WebAssembly.
# Requires the Emscripten SDK (https://emscripten.org/docs/getting_started/downloads.html).
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found. Install Emscripten and run: source ./emsdk_env.sh" >&2
  exit 1
fi

mkdir -p public/wasm

emcc cpp/class_manager.cpp cpp/bindings.cpp \
  -std=c++17 -O2 -lembind \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createClassModule \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s STACK_SIZE=1048576 \
  -o public/wasm/class_manager.js

echo "Built public/wasm/class_manager.js and class_manager.wasm"

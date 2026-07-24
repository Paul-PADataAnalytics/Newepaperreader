#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

echo "=== Building WebAssembly Demo (docs/) ==="

mkdir -p docs

# Locate source C++ and C files (excluding .orig and examples)
CPP_FILES=$(find src -name "*.cpp" ! -name "*.orig" ! -path "*/examples/*")
LIB_C_FILES=$(find lib -name "*.c" ! -path "*/examples/*")
LIB_CPP_FILES=$(find lib -name "*.cpp" ! -path "*/examples/*")

ALL_SOURCES="$CPP_FILES $LIB_C_FILES $LIB_CPP_FILES"

EMCC_FLAGS=(
    -D NATIVE_TESTING
    -D HAS_SDL
    -D MZ_MALLOC=malloc
    -D MZ_FREE=free
    -D MZ_REALLOC=realloc
    -I include
    -I src
    -I lib/miniz
    -I lib/tinyxml2
    -I .pio/libdeps/native/ArduinoJson/src
    -I src/mocks
    -I src/comm
    -I src/reader
    -I src/ui
    -I src/apps
    -I src/native
    -s USE_SDL=2
    -s WASM=1
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=67108864
    --shell-file tools/web_shell.html
    -o docs/index.html
)

# Asset preloading if directories exist
if [ -d "data" ]; then
    EMCC_FLAGS+=(--preload-file "data@/data")
fi
if [ -d "books" ]; then
    EMCC_FLAGS+=(--preload-file "books@/books")
fi
if [ -d "images" ]; then
    EMCC_FLAGS+=(--preload-file "images@/images")
fi

if command -v emcc &>/dev/null; then
    echo "Running local emcc..."
    emcc $ALL_SOURCES "${EMCC_FLAGS[@]}"
elif command -v docker &>/dev/null; then
    echo "Running emcc via Docker (emscripten/emsdk)..."
    docker run --rm -v "$ROOT_DIR":/src -w /src emscripten/emsdk emcc $ALL_SOURCES "${EMCC_FLAGS[@]}"
else
    echo "Error: Neither 'emcc' nor 'docker' were found on system."
    exit 1
fi

echo "=== WebAssembly build complete! Static assets updated in docs/ ==="
ls -lh docs/

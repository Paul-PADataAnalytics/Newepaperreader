#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

if [[ ! -f .pio/libdeps/native/ArduinoJson/src/ArduinoJson.h ]]; then
    echo "Missing ArduinoJson. Install the native environment dependencies with:" >&2
    echo "  pio pkg install -e native" >&2
    exit 1
fi

mapfile -d '' SOURCES < <(
    find src lib/miniz lib/tinyxml2 lib/tjpgd -type f \( -name '*.cpp' -o -name '*.c' \) \
        ! -name '*.orig' ! -path '*/examples/*' -print0
)
SOURCES+=(tools/web_main.cpp)

EMCC_FLAGS=(
    -D NATIVE_TESTING
    -D HAS_SDL
    -D main=firmware_native_main
    -D MZ_MALLOC=malloc
    -D MZ_FREE=free
    -D MZ_REALLOC=realloc
    -I include
    -I src
    -I src/mocks/web
    -I lib/miniz
    -I lib/tinyxml2
    -I lib/tjpgd
    -I .pio/libdeps/native/ArduinoJson/src
    -I src/mocks
    -I src/comm
    -I src/reader
    -I src/ui
    -I src/apps
    -I src/native
    -s WASM=1
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=67108864
    --shell-file tools/web_shell.html
    -o docs/index.html
)

# Only package deliberate, checked-in demo fixtures. Local books and ignored
# configuration files must never be embedded in a public GitHub Pages build.
WEB_ASSETS=(
    data/Roboto-Regular.ttf
    data/app_icons.bin
    data/books/test.md
    data/books/test_document.rtf
    data/images/fullhd.jpg
    data/images/grayscale.jpg
    data/images/landscape_1_1784804480098.jpg
    data/images/landscape_2_1784804506450.jpg
    data/images/large.jpg
    data/images/medium.jpg
    data/images/portrait.jpg
    data/images/screen_exact.jpg
    data/images/small.jpg
    data/images/tiny.jpg
    data/images/xlarge.jpg
    images/screen_exact.jpg
)
for asset in "${WEB_ASSETS[@]}"; do
    if [[ ! -f "$asset" ]]; then
        echo "Missing required demo asset: $asset" >&2
        exit 1
    fi
    EMCC_FLAGS+=(--preload-file "$asset@/$asset")
done

mkdir -p docs

if command -v emcc >/dev/null 2>&1; then
    echo "Building firmware with local Emscripten..."
    em++ "${SOURCES[@]}" "${EMCC_FLAGS[@]}"
elif command -v docker >/dev/null 2>&1; then
    echo "Building firmware with emscripten/emsdk via Docker..."
    docker run --rm \
        -v "$ROOT_DIR:/src" \
        -w /src \
        emscripten/emsdk:latest \
        em++ "${SOURCES[@]}" "${EMCC_FLAGS[@]}"
else
    echo "Error: install Emscripten or Docker with emscripten/emsdk." >&2
    exit 1
fi

echo "WebAssembly firmware built in docs/."
ls -lh docs/index.html docs/index.js docs/index.wasm docs/index.data

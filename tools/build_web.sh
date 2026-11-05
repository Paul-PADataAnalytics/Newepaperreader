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

mapfile -d '' CPP_SOURCES < <(
    find src lib/miniz lib/tinyxml2 lib/tjpgd -type f -name '*.cpp' \
        ! -name '*.orig' ! -path '*/examples/*' -print0
)
CPP_SOURCES+=(tools/web_main.cpp)
mapfile -d '' C_SOURCES < <(
    find src lib/miniz lib/tinyxml2 lib/tjpgd -type f -name '*.c' \
        ! -name '*.orig' ! -path '*/examples/*' -print0
)
# em++ compiles .c files as C++ (tjpgd then fails on void* conversions), so
# the C sources are compiled with emcc first and linked in as objects.
OBJ_DIR=.pio/build/web_c
C_OBJECTS=()
for src in "${C_SOURCES[@]}"; do
    C_OBJECTS+=("$OBJ_DIR/$(echo "${src%.c}" | tr / _).o")
done

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
    -O2
    -s WASM=1
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=67108864
    -s FORCE_FILESYSTEM=1
    -s EXPORTED_RUNTIME_METHODS=FS,IDBFS,ccall,addRunDependency,removeRunDependency
    -lidbfs.js
    --shell-file tools/web_shell.html
    -o docs/index.html
)

# Only package deliberate, checked-in demo fixtures. Local books and ignored
# configuration files must never be embedded in a public GitHub Pages build.
# Each entry is "source:path on the simulated SD card". The page seeds the SD
# card (stored in the browser's IndexedDB) from this bundle on first start.
WEB_ASSETS=(
    "data/Roboto-Regular.ttf:data/Roboto-Regular.ttf"
    "data/books/test.md:books/test.md"
    "data/books/test_document.rtf:books/test_document.rtf"
    "tools/web_demo/library/Alice's Adventures in Wonderland.epub:books/Alice's Adventures in Wonderland.epub"
    "tools/web_demo/library/The Time Machine.epub:books/The Time Machine.epub"
    "tools/web_demo/library/The Adventures of Sherlock Holmes.epub:books/The Adventures of Sherlock Holmes.epub"
    "data/images/grayscale.jpg:images/grayscale.jpg"
    "data/images/landscape_1_1784804480098.jpg:images/landscape.jpg"
    "data/images/portrait.jpg:images/portrait.jpg"
    "data/images/screen_exact.jpg:images/screen_exact.jpg"
    "data/images/medium.jpg:images/medium.jpg"
)
for asset in "${WEB_ASSETS[@]}"; do
    src="${asset%%:*}"
    dst="${asset#*:}"
    if [[ ! -f "$src" ]]; then
        echo "Missing required demo asset: $src" >&2
        exit 1
    fi
    EMCC_FLAGS+=(--preload-file "$src@/bundle/$dst")
done

mkdir -p docs

# Runs an Emscripten tool locally, or in the emsdk Docker image.
if command -v emcc >/dev/null 2>&1; then
    echo "Building firmware with local Emscripten..."
    em() { "$@"; }
elif command -v docker >/dev/null 2>&1; then
    echo "Building firmware with ${EMSDK_IMAGE:-emscripten/emsdk:6.0.13} via Docker..."
    em() {
        docker run --rm --user "$(id -u):$(id -g)" -v "$ROOT_DIR:/src" -w /src \
            "${EMSDK_IMAGE:-emscripten/emsdk:6.0.13}" "$@"
    }
else
    echo "Error: install Emscripten or Docker with emscripten/emsdk." >&2
    exit 1
fi

mkdir -p "$OBJ_DIR"
# Defines, include paths and optimisation from EMCC_FLAGS (-D/-I are
# separate words followed by their value).
C_FLAGS=()
for ((i = 0; i < ${#EMCC_FLAGS[@]}; i++)); do
    f="${EMCC_FLAGS[$i]}"
    if [[ "$f" == "-D" || "$f" == "-I" ]]; then
        C_FLAGS+=("$f" "${EMCC_FLAGS[$((i + 1))]}")
        i=$((i + 1))
    elif [[ "$f" == -D* || "$f" == -I* || "$f" == "-O2" ]]; then
        C_FLAGS+=("$f")
    fi
done
for i in "${!C_SOURCES[@]}"; do
    em emcc -c "${C_SOURCES[$i]}" "${C_FLAGS[@]}" -o "${C_OBJECTS[$i]}"
done
em em++ "${CPP_SOURCES[@]}" "${C_OBJECTS[@]}" "${EMCC_FLAGS[@]}"

echo "WebAssembly firmware built in docs/."
ls -lh docs/index.html docs/index.js docs/index.wasm docs/index.data

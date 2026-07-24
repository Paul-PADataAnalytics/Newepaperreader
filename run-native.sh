#!/bin/bash
set -e

# Build the native target
pio run -e native

# Check if build succeeded
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

# Ensure data directory exists
mkdir -p data

# Run the compiled binary inside data/ as the root SD card
cd data
../.pio/build/native/program "$@"

#!/bin/bash

# Build the native target
pio run -e native

# Check if build succeeded
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

# Run the compiled binary and pass any arguments (e.g. --debug-screen)
./.pio/build/native/program "$@"

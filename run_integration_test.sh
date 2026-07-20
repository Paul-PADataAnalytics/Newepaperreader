#!/bin/bash
set -e

# Build the native ESP32 mock
source .venv/bin/activate
pio run -e native

# Run the native ESP32 mock in the background
./.pio/build/native/program &
MOCK_PID=$!

# Give it a second to start
sleep 2

# Run the Dart test
cd android_app
dart run test/tcp_mock_test.dart
TEST_EXIT_CODE=$?

# Kill the mock server
kill $MOCK_PID

exit $TEST_EXIT_CODE

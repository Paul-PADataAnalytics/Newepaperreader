#!/bin/bash

# Exit immediately if any command fails
set -e

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}==> Starting Android APK Build Automation...${NC}"

# Determine target directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/android_app"

if [ ! -d "$PROJECT_DIR" ]; then
    echo -e "${RED}Error: android_app directory not found at $PROJECT_DIR${NC}"
    exit 1
fi

cd "$PROJECT_DIR"

# Check if flutter command is available
if ! command -v flutter &> /dev/null; then
    echo -e "${RED}Error: flutter command not found. Please verify Flutter is installed and added to your PATH.${NC}"
    exit 1
fi

# Step 1: Clean build cache
echo -e "${GREEN}==> Cleaning build artifacts...${NC}"
flutter clean

# Step 2: Fetch dependencies
echo -e "${GREEN}==> Fetching package dependencies...${NC}"
flutter pub get

# Step 3: Build debug APK
echo -e "${GREEN}==> Compiling Debug APK...${NC}"
flutter build apk --debug

# Verify output APK exists
APK_PATH="build/app/outputs/flutter-apk/app-debug.apk"
if [ -f "$APK_PATH" ]; then
    ABS_APK_PATH="$(pwd)/$APK_PATH"
    echo -e "${GREEN}==> Success! APK generated at:${NC}"
    echo -e "${GREEN}$ABS_APK_PATH${NC}"
else
    echo -e "${RED}Error: APK build completed but $APK_PATH was not found!${NC}"
    exit 1
fi

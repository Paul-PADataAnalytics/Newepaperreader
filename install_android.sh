#!/bin/bash

# Exit on error
set -e

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}==> Starting Android APK Deployment Automation via ADB...${NC}"

# Determine paths relative to script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APK_PATH="$SCRIPT_DIR/android_app/build/app/outputs/flutter-apk/app-debug.apk"

# Check if the APK exists
if [ ! -f "$APK_PATH" ]; then
    echo -e "${RED}Error: APK file not found at $APK_PATH${NC}"
    echo -e "${YELLOW}Please build the APK first by running: ./build_android.sh${NC}"
    exit 1
fi

# Check if ADB is installed
if ! command -v adb &> /dev/null; then
    echo -e "${RED}Error: 'adb' command not found. Please install Android Platform Tools and add 'adb' to your PATH.${NC}"
    exit 1
fi

# Check if any Android devices are connected
echo -e "${GREEN}==> Scanning for connected USB/ADB devices...${NC}"
DEVICE_LIST=$(adb devices | tail -n +2 | grep -v '^$')

if [ -z "$DEVICE_LIST" ]; then
    echo -e "${RED}Error: No connected Android devices detected.${NC}"
    echo -e "${YELLOW}Please connect your phone via USB, enable Developer Options, and turn on USB Debugging.${NC}"
    exit 1
fi

# Check for unauthorized devices
if echo "$DEVICE_LIST" | grep -q "unauthorized"; then
    echo -e "${RED}Error: Connected device is unauthorized.${NC}"
    echo -e "${YELLOW}Please check your phone's screen and allow USB debugging authorization for this computer.${NC}"
    exit 1
fi

# Output connected devices
echo -e "Found device(s):"
echo "$DEVICE_LIST"
echo ""

# Install the APK
echo -e "${GREEN}==> Copying and installing APK onto phone...${NC}"
# adb install flags:
# -r: replace existing application (keep data)
# -d: allow version code downgrade (in case a newer version is already on the phone)
if adb install -r -d "$APK_PATH"; then
    echo -e "${GREEN}==> Success! The companion app was successfully installed and launched.${NC}"
else
    echo -e "${RED}Error: Installation failed.${NC}"
    exit 1
fi

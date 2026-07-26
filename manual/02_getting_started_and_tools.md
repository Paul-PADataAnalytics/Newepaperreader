# Chapter 2: Device Flashing, Native Linux Build, Simulator & SD Card Setup

## Overview
This chapter provides complete documentation for flashing the physical LilyGo T5-ePaper-S3 device, running and configuring the Native Linux build (dual-window SDL2 mode & command line parameters), controlling the simulator via `simctl`, and managing the SD card structure and file locations.

---

## 1. Flashing the Physical Device

### Prerequisites
- **PlatformIO CLI** or **VS Code PlatformIO Extension**.
- USB-C Data Cable connected to the **ESP32-S3 USB port** on the LilyGo T5-ePaper-S3.

### Build and Upload Commands

```bash
# 1. Compile firmware for LilyGo T5-ePaper-S3
pio run -e t5-47-s3

# 2. Flash firmware to connected board over USB-C
pio run -e t5-47-s3 -t upload

# 3. Open serial output monitor at 115200 baud
pio device monitor -b 115200
```

### Manual Flashing / Recovery Mode
If the device does not enter flash mode automatically:
1. Hold down the **BOOT button** (`GPIO 0`).
2. Press and release the **RESET button** (`RST`).
3. Release the **BOOT button**. The ESP32-S3 will enter the ROM bootloader for flashing.

---

## 2. Native Linux Build (Launching, Features & Parameters)

The firmware can be compiled natively on desktop Linux using PlatformIO's `native` environment via SDL2.

### Compiling and Launching

```bash
# Compile native binary
pio run -e native

# Launch interactive simulator with GUI window
./.pio/build/native/program --window
```

---

## 3. Simulator Control CLI (`tools/simctl`)

```bash
tools/simctl start
tools/simctl launch "File Browser"
tools/simctl ui
tools/simctl tap 350 90
tools/simctl screenshot /tmp/file-options.png
tools/simctl stop
```

---

## 4. MicroSD Card Management & Important File Locations

### Preparing SD Card Storage (`tools/prepare_sd.sh`)

```bash
# Make script executable
chmod +x tools/prepare_sd.sh

# Prepare a physical SD card mounted at /media/user/SDCARD
./tools/prepare_sd.sh /media/user/SDCARD

# Prepare local simulator fixture
./tools/prepare_sd.sh /tmp/newepaperreader-sim-sd
```

### Important SD Card File Locations

```
/sd/
├── data/
│   ├── app_icons.bin        # 4-bit 16-grayscale packed launcher icons (6912 bytes)
│   ├── Roboto-Regular.ttf   # System TrueType font for typography engine
│   └── system_state.txt     # Deep-sleep resume breadcrumb (active app index, book path/page state, orientation)
├── books/
│   ├── classic/             # EPUB / TXT classic books directory (deep scanned)
│   └── scifi/               # EPUB / TXT sci-fi books directory (deep scanned)
└── images/                  # Gallery image directory (JPEG / RAW dithered images)
```

# LilyGo EPD47 E-Reader System v3.0.0

Welcome to the LilyGo EPD47 E-Reader System **v3.0.0**! This is the complete custom firmware for the LilyGo T5-ePaper-S3 (4.7" E-Ink display) built using PlatformIO.

**New in v3.0**: portrait-only redesigned UI, configurable auto-sleep (also while reading) with a much lower-power deep sleep, a "stay awake on USB" and developer "always on" option, brightened and dithered pictures, and a rebuilt companion-app link (working device library, uploads with progress, delete). See [Chapter 9](manual/09_power_and_companion.md).

![Launcher](images/screen_exact.jpg)

📖 **[Read the Complete User & Hardware Manual](manual/index.md)** (Firmware Version: v3.0.0)

---

## Complete Manual Chapters

- **[Chapter 1: Hardware Specifications & Architecture](manual/01_hardware_overview.md)** (v3.0.0): ESP32-S3, ED047TC1 960x540 E-Paper, Goodix GT911 touch, MicroSD SPI, pinouts & critical power precautions.
- **[Chapter 2: Device Flashing, Native Linux Build, Simulator & SD Setup](manual/02_getting_started_and_tools.md)** (v3.0.0): Flashing guide, native Linux build parameters, `simctl` CLI, and SD locations.
- **[Chapter 3: Home Screen](manual/03_launcher_app.md)** (v3.0.0): Clock, continue-reading card, app tiles and navigation.
- **[Chapter 4: E-Reader Application](manual/04_ereader_app.md)** (v3.0.0): Deep directory scanning, progress meter, typography engine, and BUTTON_1 deep-sleep lock mode with exact book/chapter/page resume.
- **[Chapter 5: eBookmark Companion App](manual/05_ebookmark_app.md)** (v3.0.0): Reading tracking analytics, Bluetooth Low Energy (BLE) sync server (`LilyGo-eReader-BLE`), and wireless book transfers.
- **[Chapter 6: File Browser Application](manual/06_file_browser_app.md)** (v3.0.0): Folder browsing, File Options modal (Move, Copy, Delete, Cancel), and safety confirmation dialogs.
- **[Chapter 7: Pictures](manual/07_image_viewer_app.md)** (v3.0.0): JPEG viewing with adjustable brightening and dithering.
- **[Chapter 8: Calculator & Settings Applications](manual/08_calculator_and_settings.md)** (v3.0.0): Calculator and the Display / Power / Bluetooth / About settings.
- **[Chapter 9: Power, Sleep & the Companion Protocol](manual/09_power_and_companion.md)** (v3.0.0): Sleep design, BLE protocol v2 and developer tools.
- **[Chapter 10: Games](manual/10_games.md)** (v3.0.0): Tea Race 1866, The Keeper and Codebreaker 1917.

---

## Quick Start Commands

### 1. Build & Flash Hardware (ESP32-S3)
```bash
pio run -e t5-47-s3
pio run -e t5-47-s3 -t upload -t monitor
```

### 2. Desktop Native Linux Simulator
```bash
pio run -e native
./.pio/build/native/program
```

### 3. WebAssembly Interactive Web Demo
```bash
# Build static WebAssembly site into docs/
./tools/build_web.sh

# Serve static site locally
python3 -m http.server 8080 -d docs/
```

The simulator runs the same C++ firmware as WebAssembly. See [browser build
and GitHub Pages setup](manual/web_simulator.md). The build and deployment
workflow follows `paul-padataanalytics-power-ui-revamp-companion-fixes`, not
`main`; do not publish the old main-branch demo.

### Refresh and battery behavior

UI partial updates erase and redraw only the changed, byte-aligned physical
region in grayscale. Opening and closing reader overlays uses a clean refresh.
The panel stays powered only while drawing. Idle light sleep retains the
visible screen and touch wake; enabled BLE or Wi-Fi pauses it. BLE defaults
off for new settings files, but existing preferences are preserved. See
[power settings and wake trade-offs](manual/09_power_and_companion.md).

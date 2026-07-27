# LilyGo EPD47 E-Reader System v2.3.0

Welcome to the LilyGo EPD47 E-Reader System **v2.3.0**! This is the complete custom firmware for the LilyGo T5-ePaper-S3 (4.7" E-Ink display) built using PlatformIO.

![Launcher v2.3](file:///home/paul/Documents/projects/newepaperreader/images/screen_exact.jpg)

📖 **[Read the Complete User & Hardware Manual](manual/index.md)** (Firmware Version: v2.3.0)

---

## Complete Manual Chapters

- **[Chapter 1: Hardware Specifications & Architecture](manual/01_hardware_overview.md)** (v2.3.0): ESP32-S3, ED047TC1 960x540 E-Paper, Goodix GT911 touch, MicroSD SPI, pinouts & critical power precautions.
- **[Chapter 2: Device Flashing, Native Linux Build, Simulator & SD Setup](manual/02_getting_started_and_tools.md)** (v2.3.0): Flashing guide, native Linux build parameters, `simctl` CLI, and SD locations.
- **[Chapter 3: Launcher Home Screen](manual/03_launcher_app.md)** (v2.3.0): Status bar metrics, custom 4-bit 16-grayscale packed icons (`app_icons.bin`), and card shortcut grid.
- **[Chapter 4: E-Reader Application](manual/04_ereader_app.md)** (v2.3.0): Deep directory scanning, progress meter, typography engine, and BUTTON_1 deep-sleep lock mode with exact book/chapter/page resume.
- **[Chapter 5: eBookmark Companion App](manual/05_ebookmark_app.md)** (v2.3.0): Reading tracking analytics, Bluetooth Low Energy (BLE) sync server (`LilyGo-eReader-BLE`), and wireless book transfers.
- **[Chapter 6: File Browser Application](manual/06_file_browser_app.md)** (v2.3.0): Folder browsing, File Options modal (Move, Copy, Delete, Cancel), and safety confirmation dialogs.
- **[Chapter 7: Image Viewer Application](manual/07_image_viewer_app.md)** (v2.3.0): Grayscale dithered JPEG/RAW image rendering, zoom, and rotation controls.
- **[Chapter 8: Calculator & Settings Applications](manual/08_calculator_and_settings.md)** (v2.3.0): Standard arithmetic math calculator and system settings configuration.

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

🌐 The prebuilt demo in `docs/` is automatically deployed to **GitHub Pages** on every push to `main` via the `deploy-github-pages.yml` workflow. Once Pages is enabled in the repository settings (source: GitHub Actions), the live demo will be available at `https://paul-padataanalytics.github.io/Newepaperreader/`.

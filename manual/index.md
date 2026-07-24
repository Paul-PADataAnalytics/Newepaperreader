# LilyGo EPD47 E-Reader System v2.2 - User & Developer Manual

Welcome to the official 8-chapter reference manual for the **LilyGo EPD47 E-Reader System v2.2** firmware running on the LilyGo T5-ePaper-S3.

> **Firmware Version**: v2.2.0  
> **Last Verified**: 2026-07-24  

---

## Manual Table of Contents

### Hardware & System Setup
- **[Chapter 1: Hardware Specifications & Architecture](01_hardware_overview.md)** (v2.2.0)  
  *ESP32-S3-WROOM-1, ED047TC1 960x540 E-Paper, Goodix GT911 touch, MicroSD SPI, pinouts, hardware porting notes, and critical power rail precautions.*
- **[Chapter 2: Device Flashing, Native Linux Build, Simulator & SD Card Setup](02_getting_started_and_tools.md)** (v2.2.0)  
  *Flashing commands (`pio run -e t5-47-s3 -t upload`), native Linux build parameters (`--window`, `--sd-root`), simulator daemon CLI (`simctl`), and SD card locations.*

---

### Application User Guides

- **[Chapter 3: Launcher Home Screen](03_launcher_app.md)** (App Version: v2.2.0)  
  *Top status bar (time, battery %, RAM), 3x2 grid layout, and custom 4-bit 16-level grayscale card icons.*
- **[Chapter 4: E-Reader Application](04_ereader_app.md)** (App Version: v2.2.0)  
  *Deep directory scanning, progress meter, library sorting (Author, Genre, Prog %), typography engine, and BOOT standby state restoration.*
- **[Chapter 5: eBookmark Companion App](05_ebookmark_app.md)** (App Version: v2.2.0)  
  *Reading tracking analytics, Bluetooth Low Energy (BLE) sync server (`LilyGo-eReader-BLE`), and wireless EPUB book transfers.*
- **[Chapter 6: File Browser Application](06_file_browser_app.md)** (App Version: v2.2.0)  
  *Folder navigation, File Options action modal (Move, Copy, Delete, Cancel), and safety confirmation modals.*
- **[Chapter 7: Image Viewer Application](07_image_viewer_app.md)** (App Version: v2.2.0)  
  *JPEG & RAW image rendering, Floyd-Steinberg 16-level grayscale dithering, and zoom/rotation controls.*
- **[Chapter 8: Calculator & Settings Applications](08_calculator_and_settings.md)** (App Version: v2.2.0)  
  *Standard arithmetic math calculator and system-wide settings preferences.*

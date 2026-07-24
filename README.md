# LilyGo EPD47 E-Reader System v2.2.0

Welcome to the LilyGo EPD47 E-Reader System **v2.2.0**! This is the complete custom firmware for the LilyGo T5-ePaper-S3 (4.7" E-Ink display) built using PlatformIO.

![Launcher v2.2](file:///home/paul/Documents/projects/newepaperreader/images/screen_exact.jpg)

## System Overview & Key Features

- **App Grid Launcher with Icons**: Visual 2x3 app grid with embedded 48x48 16-level grayscale icons.
- **Full Application Suite**:
  - **E-Reader**: Full EPUB and text reading with customizable font sizing and pagination.
  - **eBookmark Sync**: BLE synchronization with mobile companion apps for reading progress tracking.
  - **File Manager**: Comprehensive SD card file and directory browser.
  - **Image Viewer**: High-contrast JPEG and RAW image rendering engine.
  - **Calculator**: High-contrast basic math utility.
  - **Settings App**: Wireless and system configuration portal.
- **Advanced Display HAL**: Custom 2-pass partial update engine to eliminate ghosting while preserving E-Ink lifespan.
- **Hardware Documentation**: Comprehensive [User Manual](file:///home/paul/Documents/projects/newepaperreader/manual/index.md) including hardware constraints and developer safety notes.

## Hardware & Firmware Safety

For detailed hardware specifications, RTC wakeup constraints, and critical E-Ink driver IC timing notes, please refer to:
👉 [01 - Hardware Overview & Critical Developer Notes](file:///home/paul/Documents/projects/newepaperreader/manual/01_hardware_overview.md)

## Building the Firmware

### Hardware Build (ESP32-S3)
```bash
pio run -e t5-47-s3
pio run -e t5-47-s3 -t upload -t monitor
```

### Desktop Native Simulator
```bash
pio run -e native
./.pio/build/native/program
```

# LilyGo EPD47 E-Reader System v2.0

Welcome to the LilyGo EPD47 E-Reader System v2.0! This is a complete custom firmware for the LilyGo T5-ePaper-S3 (4.7" E-Ink display) built using PlatformIO.

## Key Features

### Core System
- **Power Efficiency**: Extreme low-power Light Sleep mode activates after 30 seconds of inactivity. Powers down the E-Ink display and halts the CPU, waking instantly on touch (via GT911 hardware interrupt).
- **Advanced Display Framework**: Implements a custom 2-pass partial update system to reduce screen flashing, along with full-screen hardware refreshes when needed.
- **Dark Mode**: System-wide dark mode inversion support.
- **Native Simulator**: Develop and test UI components directly on your PC using the `native` PlatformIO environment (via SDL2), before deploying to the ESP32.

### Included Applications
- **Launcher**: A dynamic home screen displaying battery life, current time, and an app grid.
- **E-Reader**: A fully-featured EPUB reader utilizing a custom typography engine. Supports pagination, adjustable font sizes, and screen rotation.
- **eBookmarks**: Rich bookmarking system that saves your exact progress (page, percentage) and synchronizes seamlessly.
- **Companion App Sync**: Built-in Bluetooth Low Energy (BLE) server to communicate with mobile companion apps. Supports two-way bookmark syncing and wireless EPUB book transfers.
- **Image Viewer**: Render and navigate through `.raw` high-contrast images directly from the SD card.
- **Calculator**: A built-in standard arithmetic calculator.
- **Timer**: Includes a local Clock, a Stopwatch with lap times, and a Countdown Timer.
- **Settings**: System configuration interface to manage defaults (like E-Reader font size and Dark mode preferences).

## Building the Project

This project uses PlatformIO. 

### Dependencies
All required libraries (such as `LilyGo-EPD47`, `AsyncTCP`, `ArduinoJson`, etc.) are automatically managed by PlatformIO.

### Compile for Hardware (ESP32-S3)
```bash
pio run -e t5-47-s3
pio run -e t5-47-s3 -t upload
```

### Compile for Native Simulator (Desktop)
```bash
pio run -e native
.pio/build/native/program
```

## Hardware Requirements
* **Board**: LilyGo T5-ePaper-S3 (ESP32-S3)
* **Display**: 4.7-inch E-Ink Display (960x540)
* **Touch**: GT911 Capacitive Touch Controller
* **Storage**: MicroSD Card (for books and images)

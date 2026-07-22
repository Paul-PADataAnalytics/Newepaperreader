# LilyGo E-Reader & NER Companion System (v1.02a)

A custom e-paper reader operating environment designed for the **LilyGO T5-ePaper-S3 (EPD47)** board, paired with the **NER Companion** Android mobile application.

---

## Architecture Overview

### 1. Embedded Firmware (ESP32-S3 / C++ / PlatformIO)
- **Modular App Platform Framework**: Managed by a central `Launcher` that coordinates `onCreate`, `onDestroy`, `draw`, `handleTouch`, and `update` across applications.
- **6 Applications**:
  1. **E-Reader App**: Reads EPUB, TXT, and RTF books from SD card (`/books/`). Features library sorting (Author, Genre, Completion %), bookmarks, font rendering, custom genre dropdowns, and configurable reading font size.
  2. **eBookmark App**: Track physical book reading progress and statistics, synchronized live via BLE.
  3. **Settings App**: Multi-page settings manager for Light/Dark Mode 16-level grayscale inversion, E-Reader reading font size (`20pt`–`40pt`), and dedicated BLE Server broadcast controls.
  4. **Calculator App**: Touch-friendly 4x5 pocket calculator grid with mathematical expression evaluation operating in portrait mode (540x960).
  5. **Timer App**: Multi-mode Clock, Stopwatch, and Countdown timer with periodic deep E-Ink screen flushes to prevent burn-in/ghosting.
  6. **Image Viewer App**: SD-card JPG image browser using `stb_image` allocated in **ESP32 PSRAM**, live 16-shade E-Ink grayscale conversion, and raw binary disk caching (`/data/cache_image.raw`).
- **2-Pass System-wide Localized Refresh Architecture**: Eliminates E-Ink ghosting by performing a 2-pass update sequence on all localized UI changes (Pass 1: wipes target bounding box to background & flushes to hardware display to reset microspheres; Pass 2: renders new crisp text and flushes to hardware).
- **30-Second Inactivity Sleep Screen**: Automatically clears screen to a blank white standby display rendering `"Sleeping zzzz"` in the center after 30 seconds of touch inactivity to prevent screen burn-in. Wakes up cleanly on touch.
- **Always-On System Service BLE**: Pre-allocated static 4KB ring buffer (zero dynamic heap allocation/deallocation overhead during runtime) with auto-reconnect advertising (`"EPD-Reader"`). Processes date/time sync, book progress sync, and database queries globally across all apps.
- **Global Escape Gesture**: Tap top-left corner (`x <= 60, y <= 60`) from any application to return to the System Launcher menu.

### 2. NER Companion Mobile App (Flutter / Android)
- **ISBN & Barcode Scanning**: Scan physical book barcodes via camera.
- **API Metadata Fetching**: OpenLibrary & Google Books API integration for automatic title, author, page count, and genre fetching.
- **Gutenberg Free Ebook Library**: Browse and download open-domain ebooks directly to the device.
- **Live BLE Synchronizer**: Auto-reconnects to `EPD-Reader`, syncs mobile device date/time to ESP32 RTC, and bi-directionally syncs reading progress and bookmarks via immediate minor delta notifications.

---

## Build & Installation Instructions

### ESP32 Firmware (PlatformIO)
Prerequisites: PlatformIO CLI or VS Code PlatformIO extension.

- **Native Desktop Mock**:
  ```bash
  pio run -e native
  ./.pio/build/native/program
  ```

- **ESP32-S3 Microcontroller Target**:
  ```bash
  pio run -e t5-47-s3 -t upload
  ```

### Android Companion App (NER Companion)
Prerequisites: Flutter SDK (^3.10.8) and Android SDK.

```bash
cd android_app
flutter pub get
flutter run
```

---

## System Configuration & Key Features

| Feature | Details |
|---|---|
| **Version Label** | `v1.02a` |
| **Inactivity Sleep Timer** | 30 seconds -> Blank screen with `"Sleeping zzzz"` |
| **Global Escape Corner** | Top-Left corner (`x <= 60, y <= 60`) |
| **Settings Cog Header** | Top-Right corner (`x >= 900, y <= 60`) |
| **Grayscale Inversion** | Dark Mode toggle in SettingsApp (`~b` / `15 - val`) |
| **BLE Service UUID** | `12345678-1234-1234-1234-123456789abc` |

---

## License & Credits
Developed for LilyGO T5-ePaper-S3 (EPD47). Includes open-source libraries: `LilyGo-EPD47`, `ArduinoJson`, `stb_image`, `tinyxml2`, and `miniz`.

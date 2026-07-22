# Project TODO & Agent Handoff Document

**Project:** `newepaperreader` — LilyGo E-Reader (ESP32-S3 EPD47) + Android Companion App (Flutter)  
**Last Updated:** 2026-07-22  

---

## 1. System Architecture Overview

### Firmware (ESP32-S3 / C++ / PlatformIO)
- **Framework:** Arduino framework on Espressif 32 (T5-ePaper-S3 board)
- **App Platform Architecture:** `Launcher` manages active `Application` lifecycle (`onCreate`, `onDestroy`, `draw`, `handleTouch`, `update`).
- **Applications Implemented (6 total):**
  1. `EReaderApp`: EPUB, TXT, RTF reader with library sorting (Author, Genre, Completion %), bookmarks, font rendering, custom genre dropdowns.
  2. `EBookmarkApp`: Track physical books & reading stats, syncs via BLE.
  3. `SettingsApp`: Wireless settings, BLE Sync Server broadcast toggle, RTC time display/sync, full E-Ink clear logging.
  4. `CalculatorApp`: Touch-friendly 4x5 pocket calculator grid with expression evaluation.
  5. `TimerApp`: Multi-mode clock, stopwatch, and countdown timer with visual alerts.
  6. `ImageViewerApp`: SD-card JPG image browser using `stb_image` allocated in **ESP32 PSRAM**, 16-shade E-Ink grayscale conversion, live progress bar, and raw binary disk caching (`/data/cache_image.raw`).

### Mobile App (Flutter / Android)
- **Features:**
  - Barcode / ISBN scanning (mobile camera).
  - OpenLibrary / Google Books API metadata lookup (Title, Author, Total Pages, Custom/Standard Genres).
  - Gutenberg free ebook browsing & downloading (with `SafeArea` navigation inset fixes).
  - BLE Sync Service (`BleService` with automatic reconnect using `SharedPreferences`, `neverForLocation` permission handling, and instant RTC unix-timestamp clock sync to ESP32 on connect).

---

## 2. Recent Completed Work Log

### Session: 2026-07-22 (Kimi K2.7 Session)
- [x] **Transition Refreshes**: Removed extra `DisplayHAL::clear()` in `Launcher::switchToApp()` to prevent double screen flashing.
- [x] **Settings Log Ghosting**: Expanded partial refresh area in `SettingsApp::drawLogLineOnly()` (`y=310, height=80`) for clean log rendering.
- [x] **Gutenberg UI Android Insets**: Wrapped download bottom sheet in `SafeArea` + `viewInsets.bottom` padding to avoid OS gesture bar clipping.
- [x] **BLE Auto-Reconnect**: Implemented `SharedPreferences` last-device ID persistence in `BleService` & auto-connect on app startup.
- [x] **Device RTC Time Sync**: Sent `{"t":"TIME","val":<unix_timestamp>}` on BLE connect; firmware updates ESP32 system clock via `settimeofday()`.
- [x] **Two-way Sync on App Open**: `EBookmarkApp::onCreate()` triggers `syncToApp()` when BLE is initialized.

### Session: 2026-07-22 (Antigravity Session)
- [x] **Calculator App**: Created `CalculatorApp.h` / `CalculatorApp.cpp`. High-contrast E-Ink layout.
- [x] **Timer App**: Created `TimerApp.h` / `TimerApp.cpp`. Clock, Stopwatch, Countdown modes.
- [x] **Image Viewer App**: Created `ImageViewerApp.h` / `ImageViewerApp.cpp`. JPG decoding, PSRAM allocations, 16-level grayscale conversion state machine with progress bar, `/data/cache_image.raw` caching.
- [x] **Launcher Dashboard 3x2 Grid**: Redesigned launcher menu to display 6 cards in a 3x2 grid (`cardW=280, cardH=170`).
- [x] **Settings Cog Touch Overlap**: Shifted Settings cog from `x=920` to `x=860` (`x: 840..900`) to avoid overlapping the Global Home corner gesture (`x>=900, y<=60`).
- [x] **Font Linkage Fix**: Fixed `data_Roboto_Regular_ttf` external linkage issue in `embedded_font.cpp`.

---

## 3. Active TODO & Backlog

- [x] **EBookmark App Library Ghosting & Refresh**:
  - [x] Clear entire framebuffer & screen when moving back from book detail view to library.
  - [x] Ensure BLE book sync updates while viewing the library perform a full clean redraw of all rows.
- [x] **EBookmark Detail Partial Updates**:
  - [x] Implement fast partial update (no full E-Ink screen flash) when incrementing/decrementing page counts (+1, -1, +5, -5).
  - [x] Reserve full screen clear for initial entry into Detail view.
- [x] **Global Home Gesture Relocation to Top-Left**:
  - [x] Relocate Launcher escape gesture to Top-Left corner (`x <= 60, y <= 60`).
  - [x] Restore app header action buttons (Settings cog) to Top-Right (`x >= 900, y <= 60`).
  - [x] Shift header title text start position to `x = 70` to prevent visual overlap with Top-Left escape gesture.
- [x] **BLE Date & Time Synchronization**:
  - [x] Implemented global `AppComm::poll()` / `AppComm::getNextMessage()` background processor for incoming `{"t":"TIME","val":<unix_seconds>}` BLE messages to update system RTC (`settimeofday`) regardless of active app.
  - [x] Added explicit "Sync Device Date & Time" button in Android companion app `ble_sync_screen.dart`.
- [x] **Timer App Ghosting & Burn-in Prevention**:
  - [x] Added `UIFramework::clearArea()` and periodic deep E-Ink screen refresh (`DisplayHAL::clear()`) every 30 seconds to prevent digit ghosting/burn-in.
  - [x] Added hardware screen clear on app exit (`onDestroy()`).
- [x] **Launcher Title Date & Time Display**:
  - [x] Added live Date and Time display (`strftime("%a %b %d | %I:%M %p")`) to the Launcher top status bar.
  - [x] Automatically update status bar in `Launcher::loop()` when the minute changes.
- [x] **Always-On System Service BLE & Automatic Sync**:
  - [x] Initialized BLE system service continuously on system startup in `main.cpp` (`AppComm::init()`).
  - [x] Fixed-size preallocated static ring buffer (8 slots x 512 bytes = 4KB static compile-time memory, zero dynamic heap allocation/deallocation overhead during runtime).
  - [x] Implemented `BLEServerCallbacks::onDisconnect` to automatically restart advertising as `"EPD-Reader"` whenever a connection drops so the companion app can reconnect anytime.
  - [x] Continuous background synchronization: global `AppComm::poll()` parses `TIME` (system RTC clock), `BOOK` (eBookmark update & SD save), and `GET_BOOKMARKS` (replies with bookmark database) across all applications.
- [x] **Settings App BLE Page Refactor**:
  - [x] Main Settings screen: replace BLE card with `"BLE Settings: [Connected/Disconnected]  [Open BLE Settings]"` status line and button.
  - [x] Sub-page: dedicated BLE Settings page with broadcast toggle, status details, logs, and manual sync action buttons.
- [x] **Light Mode / Dark Mode Grayscale Inversion**:
  - [x] Add `"Light/Dark Mode: [Toggle Mode]"` setting button on main Settings page.
  - [x] Implement global 4-bit packed grayscale pixel inversion in `DisplayHAL` (`~b` / `15 - val`).
- [x] **Calculator Portrait Layout Bounds Fix**:
  - [x] Adjusted button grid bounds in `CalculatorApp.cpp` for portrait mode dimensions (`width = 540, height = 960`).
  - [x] Recalculated 4 column widths (`colW = 135px`) and 5 row heights (`rowH = 150px`) starting at `y = 200`.
  - [x] All 4 columns (`3`, `6`, `9`, `=`, `+`, `-`, `*`, `/`) now fit within `0..540` portrait width and are fully visible and touch-selectable.
- [x] **E-Reader Settings Sub-Page & Content Font Size**:
  - [x] Added `"E-Reader Settings: Font Size XXpt"` row with `[Open E-Reader Settings]` button on main Settings screen (`SettingsApp`).
  - [x] Created dedicated `PAGE_EREADER` sub-page with `-` / `+` step controls and 6 preset buttons (`20pt`, `24pt`, `28pt`, `32pt`, `36pt`, `40pt`).
  - [x] Connected static reader font size configuration (`EReaderApp::setReadingFontSize` / `getReadingFontSize`) to `EReaderApp::drawReading()` for book page text rendering.
- [x] **Release v1.01a & Code Cleanup**:
  - [x] Audited codebase for unused / unnecessary code and cleaned up temporary test files & screenshots.
  - [x] Updated Android app display name to **"NER Companion"** (`pubspec.yaml`, `AndroidManifest.xml`, `main.dart`).
  - [x] Version labeled both ESP32 firmware (`v1.01a`) and Android companion app (`1.0.1+2` / `v1.01a`).
  - [x] Created comprehensive `README.md` documentation covering system architecture, features, and build instructions.
  - [x] Staged and committed all changes in repository.
- [x] **E-Ink Localized Clear Before Update (Anti-Ghosting)**:
  - [x] Implemented 2-step localized clear pass in `EBookmarkApp::drawEBookmarkDetail()` (flushes cleared background area to E-Ink display to reset microspheres before rendering updated page numbers).
  - [x] Implemented 2-step localized clear pass in `CalculatorApp::drawDisplay()` (flushes cleared display area to E-Ink display before rendering updated expression and calculation values).
- [x] **Settings App Single Clear & Dark Mode Full Refresh**:
  - [x] Removed duplicate `DisplayHAL::clear()` on initial entry into `SettingsApp` (`Launcher::switchToApp` performs primary hardware clear).
  - [x] Added `DisplayHAL::clear()` hardware flash and full clean redraw in `SettingsApp::handleTouch()` when toggling Light/Dark mode to visually indicate mode change.
- [x] **Immediate Localized Minor Delta Sync Architecture**:
  - [x] Implemented `AppComm::sendDeltaSync()` and `AppComm::sendBookDelta()` on firmware for instant single-item JSON transmission over BLE notifications.
  - [x] Subscribed Flutter `BleService` to BLE notifications (`_readCharacteristic!.setNotifyValue(true)`), auto-parsing incoming `{"t": "<TYPE>", ...}` delta frames to update `BookStorageService` instantly for *only* the changed item.
  - [x] Established extensible architectural framework for all future syncable items.
- [x] **2-Pass System-wide Localized Refresh Audit (Anti-Burn-In & Anti-Ghosting)**:
  - [x] Implemented 2-pass partial update sequence (Pass 1: wipe target bounding box to background & flush to hardware display to reset microspheres; Pass 2: draw new content & flush to hardware display) across all localized UI updates.
  - [x] Audited `EBookmarkApp`, `CalculatorApp`, `TimerApp`, `SettingsApp`, and `Launcher` status bar.
- [x] **30-Second System Inactivity Sleep & "Sleeping zzzz" Screen**:
  - [x] Implemented 30-second user touch inactivity timer in `main.cpp`.
  - [x] On sleep: full hardware E-Ink clear (`DisplayHAL::clear()`), render blank screen with centered `"Sleeping zzzz"`, and enter low power standby to prevent screen burn-in.
  - [x] On touch: wake up, full hardware clear, and cleanly redraw active application screen.
- [x] **Release v1.02a & Code Cleanup**:
  - [x] Version labeled firmware (`v1.02a`) and Android companion app (`1.0.2+3` / `v1.02a`).
  - [x] Updated `README.md` and project documentation.
  - [x] Staged and committed release v1.02a to Git repository.
- [x] **Unified 2-Pass Localized Partial Refresh API**:
  - [x] Added `UIFramework::perform2PassPartialUpdate(framebuffer, x, y, w, h, drawFunc)` to centralize the 2-pass sequence (Pass 1: clear area to background & flush to hardware; Pass 2: draw content & flush to hardware).
  - [x] Refactored `TimerApp`, `CalculatorApp`, `EBookmarkApp`, and `SettingsApp` to use the unified API.
- [x] **Launcher Mandatory Full Refresh Rule**:
  - [x] Enforced mandatory full hardware clear (`DisplayHAL::clear()`) in `Launcher::drawMenu()` to protect the screen and eliminate any launcher partial update artifacts.
- [x] **Waking Responsiveness & Sleep Polish**:
  - [x] Reset `lastTouchTime = 0` on sleep wakeup so the very next touch input is processed immediately without any debounce delay.
- [x] **Continuous Handover Maintenance**: Ensure `TODO.md` is updated before ending turns or committing major feature blocks.

---

## 4. Build & Verification Status

| Target / Suite | Command | Result |
|---|---|---|
| **Android App (Flutter)** | `flutter analyze` | `No issues found!` |
| **Native Desktop Mock** | `pio run -e native` | `SUCCESS` |
| **ESP32 Firmware Target** | `pio run -e t5-47-s3` | `SUCCESS` |

All targets compile cleanly with zero errors. `TODO.md` will be continuously maintained across all AI agent sessions.

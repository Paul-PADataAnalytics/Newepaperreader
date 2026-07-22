# Kimi K2.7 Code Session Record

**Date:** 2026-07-22  
**Project:** newepaperreader — ESP32-S3 e-paper reader + Flutter Android companion app  

## Session Overview

This session addressed the user's most recent multi-part feedback covering transition refreshes, Gutenberg UI usability, settings log ghosting, automatic reconnect behavior, and device date/time synchronization.

## Issues Addressed

### 1. Double refresh / screen resets when moving between apps
- **Root cause:** `Launcher::switchToApp()` was calling `DisplayHAL::clear()` before the new app's `draw()`, causing two full refreshes.
- **Fix:** Removed the extra `DisplayHAL::clear()` from `switchToApp()` so the destination app performs a single clear+draw cycle.
- **File:** `src/ui/Launcher.cpp`

### 2. Settings log line ghosting when BLE state changes
- **Root cause:** Partial log updates only cleared a small region around the log text, leaving the advertising line and previous log pixels behind.
- **Fix:** Expanded the blanked region in `SettingsApp::drawLogLineOnly()` to `y=310, height=80`, covering both the advertising line and the log line. Also ensured `drawSettingsMenu(true)` is called on first draw and BLE state changes for a full clear.
- **File:** `src/apps/SettingsApp.cpp`

### 3. Gutenberg download button obscured by Android navigation buttons
- **Root cause:** The download bottom sheet did not account for system insets or the on-screen navigation bar.
- **Fix:** Wrapped the bottom sheet content in `SafeArea` and added bottom padding based on `MediaQuery.of(context).viewInsets.bottom + 32.0`.
- **File:** `android_app/lib/screens/gutenberg_screen.dart`

### 4. Android app and device do not remember/reconnect automatically
- **Root cause:** No persisted record of the previously connected BLE device.
- **Fix:**
  - Added `SharedPreferences` storage of the last connected device ID in `BleService`.
  - Added `reconnectToLastDevice()` and `autoConnect()` helpers.
  - `BleSyncScreen` now calls `_autoConnect()` on init and exposes a "Reconnect to Last Device" button.
  - Stored ID is cleared on explicit disconnect or failed reconnect.
- **Files:** `android_app/lib/services/ble_service.dart`, `android_app/lib/screens/ble_sync_screen.dart`

### 5. Device RTC not updated from Android
- **Root cause:** Android never sent its clock to the device, so bookmark history timestamps used an uninitialized RTC.
- **Fix:**
  - `BleService.connect()` now sends a `{"t":"TIME","val":<unix_seconds>}` message immediately after discovering characteristics.
  - Firmware `SettingsApp` and `EBookmarkApp` now handle `TIME` messages and call `settimeofday()` on ESP32.
  - Added `#include <sys/time.h>` where needed.
- **Files:** `android_app/lib/services/ble_service.dart`, `src/apps/SettingsApp.cpp`, `src/apps/EBookmarkApp.cpp`

### 6. Automatic two-way sync when eBookmark app opens
- **Root cause:** The firmware only pushed bookmarks when local data changed; opening the app did not advertise state to the phone.
- **Fix:** `EBookmarkApp::onCreate()` now calls `syncToApp()` if BLE is initialized, so the phone can sync as soon as the eBookmark app opens.
- **File:** `src/apps/EBookmarkApp.cpp`

## Files Modified

- `src/ui/Launcher.cpp`
- `src/apps/SettingsApp.cpp`
- `src/apps/EBookmarkApp.cpp`
- `android_app/lib/screens/gutenberg_screen.dart`
- `android_app/lib/services/ble_service.dart`
- `android_app/lib/screens/ble_sync_screen.dart`

## Build Verification

| Command | Result |
|---|---|
| `flutter analyze` | No issues |
| `pio run -e native` | Success |
| `pio run -e t5-47-s3` | Success |

## Notes

- The `TIME` sync is sent automatically on every successful connect; this ensures `last-change-wins` bookmark comparisons use correct timestamps.
- Auto-reconnect first tries the stored device ID, then falls back to scanning for `EPD-Reader`.
- The Gutenberg bottom sheet now uses `isScrollControlled: true` plus `SafeArea` padding to avoid system gesture areas.

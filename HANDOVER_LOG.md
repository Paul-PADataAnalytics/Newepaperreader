# Handover Log

## Current Status (2026-07-26) — v2.3.0
Firmware is stable, hardware-verified, and at **v2.3.0**. See [TODO.md](TODO.md) for the full session-by-session history and [manual/index.md](manual/index.md) for the user/developer manual.

Highlights of the current design (all hardware-verified):
- **Sleep/wake**: True ESP32-S3 deep sleep ("lock mode"), entered via **User Button 1** (`GPIO 21`) or after 30s of inactivity (suspended while actively reading a page). Only `BUTTON_1` (ext0 RTC wakeup) can wake the device - touch does not. Waking fully resets the chip; a breadcrumb saved to `/sd/data/system_state.txt` (see `AppStorage::SavedSystemState`) restores the active app and, for the E-Reader app, the exact book/chapter/page.
- **GPIO 0 is never used by application code** - it's reserved exclusively for the ESP32-S3 ROM bootloader (USB flashing). This was a hard-learned lesson from an earlier design that used it for a sleep button and caused boot-mode conflicts.
- **EPUB reading position** persists both the in-chapter offset and the chapter index/count (`AppStorage::BookmarkInfo`), since EPUB chapters are separate sub-files parsed independently - resuming reopens the exact chapter, not just an offset applied to a guessed chapter.

## Next Steps for the Next System
- No open bugs at time of writing. The user maintains a `version3.md` wishlist for future feature work - consult it (once created) before starting new work.
- Before making sleep/wake or GPIO changes, re-read the GPIO0 warning above and check `TODO.md`'s history for why BUTTON_1/GPIO21 was chosen instead.
- `src/DisplayHAL.cpp` / `include/DisplayHAL.h` are filesystem-locked (`chmod 444`) - do not modify without explicit user request.


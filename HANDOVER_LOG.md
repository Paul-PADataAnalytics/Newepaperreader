# Handover Log

## Current Status (2026-10-26) — v3.0.0
See [manual/index.md](manual/index.md); [Chapter 9](manual/09_power_and_companion.md) covers the power design and companion protocol in detail.

Highlights of the v3.0 rework (hardware-verified on the device):
- **Portrait only** (540 x 960). All screens use the UI kit in `src/ui/UIKit.h`; the home screen is `src/ui/Launcher.cpp`. Apps get the header back chevron via `Application::wantsSystemBack()` / `onBack()`. Hero Adventure was removed at the user's request.
- **Power**: `PowerManager` (`src/system/PowerManager.cpp`) owns auto-sleep (Settings > Power, default 5 min, applies while reading too), USB/"always on" keep-awake, CPU 240/80 MHz switching and the deep-sleep shutdown sequence. `DisplayHAL` now switches the EPD rails per refresh and puts the GT911 to sleep (INT latched low with `gpio_hold_en`) before deep sleep. The user explicitly asked for this work, which is why the previously "locked" `DisplayHAL` files were changed.
- **Settings** persist in `/data/settings.json` (`SystemSettings`).
- **Companion protocol v2** (`src/comm/AppComm.cpp`, `android_app/lib/services/ble_service.dart`): request ids, paged answers (Android caps attribute reads at 512 bytes), streamed binary uploads with back-pressure. All BLE messages are handled centrally in `AppComm::poll()` from `loop()`; apps must not consume them.
- **GPIO 0 is never used by application code** - it is the EPD driver's CFG_STR line.

## Testing
- Simulator: `pio run -e native`, run the binary with the SD root as cwd (`SDL_VIDEODRIVER=dummy` for headless), drive it with `tools/sim_client.py` (taps, screenshots, protocol).
- Device: `tools/device_console.py` (`STATUS`, taps, screenshots, `SLEEPT n` timed sleep/wake). Opening the port with pyserial's default DTR/RTS does not reset the board; clearing DTR/RTS does.
- BLE: `tools/ble_protocol_test.py --upload FILE` (needs `bleak`).

## Next Steps
- Not yet measured with a meter: actual deep-sleep current (expected ~0.4 mA per LilyGo).
- EPUB titles/authors come from file names; parsing OPF metadata would improve the library (also on the user's v3 wishlist, `Version3.0.md`).
- The web demo in `docs/` predates v3.0 and should be rebuilt with `tools/build_web.sh`.

# Chapter 9: Power, Sleep & the Companion Protocol

> **Firmware Version**: v3.0.0  
> **Last Verified**: 2026-10-26  

---

## 1. Sleep & wake

The device has two states: **awake** and **asleep** (ESP32-S3 deep sleep, "lock mode").

- **Side button** (`BUTTON_1`, GPIO 21): sleeps immediately, and is the **only** wake source. Touch never wakes the device, so it can sit in a bag.
- **Auto-sleep** (Settings > Power): after 1, 2, **5** (default), 10 or 30 minutes without a touch, or Never. This now applies everywhere, *including* while a book page is open - in v2.x the reading view never slept, which is what drained the battery in about a day.
- **Stay awake on USB** (default on): no auto-sleep while plugged in. External power is detected two ways: a computer talking to the USB port (USB Serial/JTAG sees start-of-frame packets), or the battery sitting at charger voltage (>= 4.21 V), which also covers wall chargers. While a nearly empty battery charges from a wall charger it reads below that, so auto-sleep still applies until it is almost full.
- **Waking**: the press that wakes the device is ignored until released, so holding the button a little longer no longer sends it straight back to sleep.
- **Always on** (developer option): never auto-sleep. The side button still sleeps.
- **Companion app activity** (any user-initiated command: library, upload, delete, bookmark change) counts as activity, and the device never sleeps during a file transfer. Background bookmark polling does *not* keep it awake.

Waking reboots the chip; `/data/system_state.txt` restores the open app, and the e-reader reopens the exact book, chapter and page. The home screen's *Continue reading* card comes from `/data/last_book.txt`.

### What happens before deep sleep

1. Position + breadcrumb saved, sleep screen drawn (a picture from `/images/*.raw` with a "Sleeping" banner, or a book/battery card - Settings > Power > Sleep screen).
2. BLE stopped; `epd_poweroff_all()` zeroes the whole EPD config register (LilyGo's reference sequence).
3. The GT911 touch controller is commanded into sleep (INT driven low and **latched** with `gpio_hold_en` so it stays asleep). It is woken on boot by a 10 ms high pulse on INT.
4. SD card unmounted, CS driven high and latched.
5. `ext0` wake armed on GPIO 21 (only after the button has been released).

LilyGo specifies ~0.4 mA for this board in deep sleep with button wake, which is months of standby on a typical LiPo; the previous firmware left the touch controller scanning and the panel config powered.

### Power while awake

- Panel high-voltage rails are switched on only for the duration of each refresh (they used to stay on all the time). The battery sense divider sits behind the same switch, so the voltage is sampled right after a refresh (or briefly powered for a reading at most every 5 minutes).
- The CPU runs at 240 MHz while drawing and drops to 80 MHz after 1.5 s idle (the lowest clock BLE supports).
- The main loop blocks on the touch queue instead of busy-spinning, and idle GT911 interrupt pulses (~100/s) are filtered in the touch task so they no longer wake the main loop.
- BLE advertises every 250-500 ms instead of the library default 20-40 ms.

---

## 2. Companion protocol (v2)

GATT service `12345678-1234-1234-1234-123456789abc`, advertised as `EPD-Reader`:

| Characteristic | UUID suffix | Properties | Use |
| :--- | :--- | :--- | :--- |
| Command | `...9001` | write (with response) | JSON commands and binary file frames from the phone |
| Delta | `...9002` | read, notify | Device-initiated book progress (`{"t":"BOOK",...}`) |
| Response | `...9003` | read, notify | Answers to commands, matched by `rid` |

- The phone requests MTU 512; the device accepts up to 517 and asks for a 7.5-15 ms connection interval.
- Every command may carry an integer `rid`; the answer echoes it. The device notifies the answer and also leaves it readable (the app reads if the notification is missed or truncated). Answers are kept under ~460 bytes (Android caps attribute values at 512).
- List answers are **paged**: send `off` (0 first), the answer has `items`, `total`, `off` and `next` (`-1` when done).
- Commands are queued and handled on the device's main loop. When the queue is full the device delays the write response, which throttles the phone instead of dropping data.

| Command | Fields | Answer |
| :--- | :--- | :--- |
| `HELLO` | | `{t:"HELLO", proto:2, fw, batt, usb, free}` |
| `TIME` | `val` (epoch s), `tz` (minutes east of UTC) | `{t:"TIME", ok}`; tz is persisted |
| `GET_LIBRARY` | `off`, `refresh` (rescan SD) | `LIBRARY` page of `{n: name, p: path, s: size}` - all books under `/books`, recursively |
| `GET_BOOKMARKS` | `off` | `BOOKMARKS` page of `{isbn, title, author, genre, page, total, ts}` |
| `BOOK` | `isbn, title, author, genre, page, total, ts` | `{t:"BOOK", ok, applied}` (last change wins) |
| `DELETE_FILE` | `path` (under `/books/`) or `name` | `{t:"DELETE_RESULT", ok, error?}` |
| `FILE_BEGIN` | `name`, `size`, `dir` (`books` or `images`) | `{t:"FILE_READY", ok, error?}` |
| *file data* | binary: `0x01`, u32 LE offset, bytes | none |
| `FILE_END` | `size` | `{t:"FILE_RESULT", ok, size, path, error?}` |
| `FILE_ABORT` | | `{t:"FILE_ABORT", ok}` |

Uploads are written to `/<dir>/.upload.part` in 16 KB blocks and renamed into place only after the size matches, so an interrupted transfer never leaves a half-written book in the library. A transfer stalled for 30 s, or a disconnect, aborts it. Measured: ~15 KB/s from a laptop; Android with high connection priority is similar or faster.

The native simulator speaks the same protocol over TCP port 9876 (`EPD_SIM_PORT` to change it), each frame prefixed with a little-endian u16 length.

---

## 3. Developer tools

| Tool | Purpose |
| :--- | :--- |
| `tools/device_console.py` | Serial console: `status`, `tap X Y` (on-screen coords), `shot out.png`, `sleep`, `log N`, `seq "..." "..."` |
| `tools/sim_client.py` | Drive the simulator: `tap`, `shot`, `hello`, `library`, `upload`, `delete`, `bookmarks` |
| `tools/ble_protocol_test.py` | End-to-end protocol test against the real device over BLE (needs `bleak`) |
| `tools/generate_ui_icons.py` | Regenerates `src/ui/ui_icons.h` |
| `tools/generate_sleep_images.py` | Portrait sleep pictures as `.raw` files for `/images` |

`STATUS` (and the companion `HELLO` answer) include `lastSleep`: why the device last went to sleep (button, timeout, menu, dev-command), whether a USB host was seen, the battery voltage and idle time at that moment - kept in RTC memory across deep sleep.

Serial commands understood by the firmware: `STATUS`, `S` (screenshot), `TL x y` (tap at on-screen coordinates), `T x y` (raw touch-controller coordinates), `SLEEP`, and `SLEEPT n` (sleep, and also wake after *n* seconds - for testing the resume path without pressing the button).

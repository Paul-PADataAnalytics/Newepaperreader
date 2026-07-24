# 01 - Hardware Overview & Critical Developer Notes

**Firmware Version**: v2.2.0  
**Target Hardware**: LilyGo T5-ePaper-S3 (ESP32-S3, 16MB Flash, 8MB OPI PSRAM, 4.7" ED047TC1 E-Ink Display, GT911 Touch Controller)

---

## Hardware Specifications

| Component | Specification |
| :--- | :--- |
| **Microcontroller** | Espressif ESP32-S3 Dual-Core Xtensa LX7 @ 240MHz |
| **Storage & Memory** | 16MB QIO Flash, 8MB OPI PSRAM |
| **Display Panel** | 4.7" ED047TC1 E-Paper (960x540 resolution, 16-level grayscale) |
| **Display Interface** | I80 8-bit Parallel Bus with DMA |
| **Touch Controller** | Goodix GT911 Capacitive Touch Controller (I2C address 0x14 / 0x5D) |
| **Power & Battery** | Onboard LiPo battery charger, GPIO 14 ADC voltage monitoring |
| **Wireless** | 2.4GHz Wi-Fi 802.11 b/g/n, Bluetooth LE (BLE 5.0) |

---

## Critical Developer Hardware & Firmware Warnings

> [!WARNING]
> ### 1. E-Ink Driver IC Charge Pump & Power Off Timing
> **NEVER call `epd_poweroff()` immediately after rendering commands (`epd_draw_grayscale_image`) or inside continuous UI loops.**
> 
> The LilyGo-EPD47 driver uses an asynchronous I80 DMA pipeline to push pixel frames to the panel. Calling `epd_poweroff()` prematurely cuts power to the panel's high-voltage charge pumps (~15V) *before* physical ink particles finish turning. This leaves the screen completely blank and will cause the panel's hardware controller IC to freeze into a lockup state.
> 
> **Recovery Procedure**: If the E-Ink driver IC locks up, simple soft resets or re-flashing will NOT restore the display. You must completely disconnect USB and the LiPo battery for 10 seconds to fully drain the high-voltage charge pump caps.

> [!CAUTION]
> ### 2. ESP32-S3 `ext1` Deep Sleep & Floating GPIO Restrictions
> **Do NOT configure `esp_sleep_enable_ext1_wakeup()` with non-RTC GPIO pins (such as GPIO 34), and do NOT read floating input pins in `loop()`.**
> 
> - **RTC IO Constraint**: On the ESP32-S3, `ext1` deep-sleep wakeup ONLY supports RTC-capable GPIOs (pins 0 through 21). Passing GPIO 34 to `esp_sleep_enable_ext1_wakeup()` returns `ESP_ERR_INVALID_ARG`.
> - **Floating Pin Risk**: GPIO 34 on the T5-ePaper-S3 is an input-only pin without internal pull-up resistors. Reading GPIO 34 in `loop()` without a hardware pull-up will yield floating `LOW` reads, immediately triggering sleep handlers and causing an inescapable 0ms infinite reboot loop.

---

## Navigation & Return Gesture
- **Global Escape Gesture**: Tap the top-left corner (`x <= 60, y <= 60`) from any running app to return to the Launcher menu.

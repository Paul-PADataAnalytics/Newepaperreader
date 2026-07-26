# Chapter 1: Hardware Specifications & Architecture

## Overview
The **LilyGo EPD47 E-Reader System v2.3** firmware is designed specifically for the **LilyGo T5-ePaper-S3 (T5-4.7 S3)** development board. This document details the exact hardware components, IC controllers, display parallel bus interface, touch digitizer, and peripheral pinouts to assess hardware compatibility with other ESP32 E-Paper hardware.

---

## Hardware Component Architecture

| Component | Specification | Controller / IC | Bus / Protocol |
| :--- | :--- | :--- | :--- |
| **Main SoC** | ESP32-S3-WROOM-1 | Xtensa Dual-Core LX7 @ 240 MHz | Internal System Bus |
| **System Memory** | 512 KB SRAM + 8 MB PSRAM | ESP32-S3 PSRAM | Octal SPI |
| **Flash Storage** | 16 MB SPI Flash | Winbond / Espressif Flash | Quad/Octal SPI |
| **Display Panel** | 4.7" E-Paper (960 x 540) | ED047TC1 Active Matrix | 8-bit Parallel Bus |
| **Touch Screen** | Capacitive Multi-Touch | Goodix GT911 | I2C (`SDA=GPIO6`, `SCL=GPIO5`) |
| **MicroSD Card** | SPI SD Slot | Standard SPI Controller | SPI Bus |
| **Power Mgmt** | LiPo Battery Charging & ADC | AXP2101 / TP4056 | I2C / ADC (`GPIO14`) |
| **Wireless** | 2.4GHz Wi-Fi & Bluetooth 5.0 | ESP32-S3 Integrated Radio | BLE & Wi-Fi Stack |

---

## 1. Main Processor: ESP32-S3-WROOM-1

The core microcontroller is the Espressif ESP32-S3:
- **CPU**: Dual-core 32-bit Xtensa LX7 running at 240 MHz.
- **Memory**: 512 KB internal SRAM. The firmware utilizes external PSRAM (`MALLOC_CAP_SPIRAM`) to allocate two full 960x540 8-bit framebuffers (259,200 bytes each) for smooth 2-pass localized partial update rendering.

---

## 2. E-Paper Display Interface (ED047TC1 960x540)

The display is an ED047TC1 Active Matrix E-Paper panel with $960 \times 540$ resolution supporting 16 grayscale levels.

> [!WARNING]
> **CRITICAL HARDWARE PRECAUTION**:
> Never call `epd_poweroff()` or cut display power rails while active frame rendering is in progress. Cutting EPD power rails during frame buffer transmission will cause screen freeze resets and severe image retention. Always allow the full E-Ink waveform draw cycle to finish before entering sleep states or toggling display power.

---

## 3. Touch Screen Digitizer (Goodix GT911)

Capacitive touch interface over I2C:
- **SDA**: GPIO 6
- **SCL**: GPIO 5
- **INT**: GPIO 7

Physical capacitive touch coordinates are normalized by `DisplayHAL::getTouch()` into logical app coordinates ($960 \times 540$), handling landscape and portrait rotations seamlessly.

---

## 4. MicroSD Card Interface (SPI)

The MicroSD card host operates over SPI:
- **SCK**: GPIO 14
- **MISO**: GPIO 13
- **MOSI**: GPIO 11
- **CS**: GPIO 12

### SD Storage Directory Layout
```
/sd/
├── data/
│   ├── app_icons.bin        # 4-bit 16-grayscale packed launcher icons (6912 bytes)
│   └── Roboto-Regular.ttf   # TrueType System Font
├── books/
│   ├── classic/             # Classic EPUB and text files
│   └── scifi/               # Sci-Fi EPUB and text files
└── images/                  # JPG / RAW high-contrast image files
```

---

## 5. System Buttons & GPIO Mappings

- **User Button 1 (Lock/Wake)**: `GPIO 21` (`BUTTON_1`). Pressing it while awake immediately saves a resume breadcrumb and puts the device into true ESP32-S3 deep sleep ("lock mode"); pressing it again fully resets the chip and wakes it, restoring the exact app/book/page you left. This is the *only* wake source while locked - touch input does not wake the device. See [Chapter 4](04_ereader_app.md) for full behavior.
- **GPIO 0**: Reserved exclusively for the ESP32-S3 ROM bootloader (hold during power-on/reset to flash over USB - see [Chapter 2](02_getting_started_and_tools.md)). Never used by application code - touching `GPIO0` in firmware can interfere with USB flashing/boot-mode selection.
- **Battery ADC Voltage**: `GPIO 14` / ADC (Monitors LiPo battery voltage percentage).

---

## Hardware Compatibility Notes for Porting

If porting this firmware to another ESP32 E-Paper board (e.g., LilyGo T5 2.13", Waveshare ESP32 E-Paper, or Inkplate 6):
1. **Display Driver**: Replace `LilyGo-EPD47` with the target board driver in `DisplayHAL.cpp`. Ensure `getWidth()` and `getHeight()` return actual screen dimensions.
2. **PSRAM Requirement**: External PSRAM is required for allocating the $960 \times 540$ framebuffers ($259\text{ KB}$ per buffer). Boards without PSRAM must use lower resolution screens or tiled rendering.
3. **GPIO Pinouts**: Update `SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK`, `TOUCH_SDA`, and `TOUCH_SCL` pin definitions in `DisplayHAL.h`.

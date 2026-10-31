#pragma once

#include <cstdint>

// Persistent, system-wide user preferences (stored as /data/settings.json on
// the SD card). Load once at boot via SystemSettings::load(); mutate the
// fields through get() and call save() after changing anything.
struct SystemSettingsData {
    // Auto-sleep after this many minutes without user activity. 0 = never.
    int sleepTimeoutMin = 5;
    // Development aid: never auto-sleep (the side button still sleeps).
    bool alwaysOn = false;
    // Never auto-sleep while a USB host (computer) is connected.
    bool stayAwakeOnUsb = true;
    // Bluetooth companion sync radio.
    bool bleEnabled = false;
    // Explicit light sleep retains the visible page and touch wake.
    bool idleSleep = true;
    // Image brightening filter: 0 = off (linear), 1 = light, 2 = lighter, 3 = lightest.
    int imageBrightness = 2;
    bool darkMode = false;
    float readingFontSize = 28.0f;
    // Page turns use the quick (flash-free) refresh; every this many turns a
    // full clean refresh removes ghosting. 1 = always full refresh.
    int fullRefreshEvery = 6;
    // Sleep screen: true = rotate pictures from /images (*.raw), false = info card.
    bool sleepShowsImages = true;
    // Local time offset from UTC in minutes (sent by the companion app).
    int tzOffsetMin = 0;
};

namespace SystemSettings {

SystemSettingsData& get();
void load();
bool save();

// Applies tzOffsetMin to the C library so localtime() shows local time.
void applyTimezone();

// Gamma used by the image brightening filter for the current setting.
float imageGamma();
const char* imageBrightnessLabel(int level);

} // namespace SystemSettings

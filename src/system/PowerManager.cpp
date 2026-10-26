#include "PowerManager.h"
#include "SystemSettings.h"
#include "DisplayHAL.h"

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <HWCDC.h>
#include <driver/gpio.h>
#include "utilities.h"
#else
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

namespace {
uint32_t s_lastActivity = 0;
uint32_t s_lastBoost = 0;
bool s_busy = false;
bool s_boosted = true; // Arduino boots at 240MHz
const uint32_t CPU_RELAX_AFTER_MS = 1500;
}

namespace PowerManager {

void restoreAfterWake() {
#ifndef NATIVE_TESTING
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(static_cast<gpio_num_t>(TOUCH_INT));
    gpio_hold_dis(static_cast<gpio_num_t>(SD_CS));
#endif
    s_lastActivity = millis();
}

void noteUserActivity() {
    s_lastActivity = millis();
}

void setBusy(bool busy) {
    s_busy = busy;
    if (!busy) s_lastActivity = millis();
}

uint32_t msSinceActivity() {
    return millis() - s_lastActivity;
}

bool isUsbConnected() {
#ifndef NATIVE_TESTING
    return HWCDC::isPlugged();
#else
    return false;
#endif
}

bool shouldAutoSleep() {
    const SystemSettingsData& s = SystemSettings::get();
    if (s.alwaysOn || s_busy) return false;
    if (s.sleepTimeoutMin <= 0) return false;
    if (s.stayAwakeOnUsb && isUsbConnected()) return false;
    return msSinceActivity() >= (uint32_t)s.sleepTimeoutMin * 60000u;
}

void boostCpu() {
    s_lastBoost = millis();
#ifndef NATIVE_TESTING
    if (!s_boosted) {
        setCpuFrequencyMhz(240);
        s_boosted = true;
    }
#endif
}

void idleTick() {
#ifndef NATIVE_TESTING
    // 80MHz is the lowest clock the BLE controller supports; at idle the
    // main loop just waits on the touch queue, so it loses nothing.
    if (s_boosted && millis() - s_lastBoost > CPU_RELAX_AFTER_MS) {
        setCpuFrequencyMhz(80);
        s_boosted = false;
    }
#endif
}

void prepareHardwareForDeepSleep() {
    DisplayHAL::prepareForDeepSleep();
#ifndef NATIVE_TESTING
    SD.end();
    SPI.end();
    // Keep the card deselected through sleep; a floating CS lets it wake
    // into a higher-current state.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    gpio_hold_en(static_cast<gpio_num_t>(SD_CS));
    gpio_deep_sleep_hold_en();
#endif
}

} // namespace PowerManager

#include "PowerManager.h"
#include "SystemSettings.h"
#include "DisplayHAL.h"
#include <cstdio>

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

#ifndef NATIVE_TESTING
#define RTC_KEEP RTC_DATA_ATTR
#else
#define RTC_KEEP
#endif

struct SleepRecord {
    uint32_t magic;
    uint8_t reason;
    uint8_t usbHost;
    uint16_t millivolts;
    uint32_t idleMs;
    uint32_t count;
};
static RTC_KEEP SleepRecord s_sleepRecord;
static const uint32_t SLEEP_RECORD_MAGIC = 0x51EE9A11;

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

bool isUsbHostActive() {
#ifndef NATIVE_TESTING
    return HWCDC::isPlugged();
#else
    return false;
#endif
}

bool isUsbConnected() {
    if (isUsbHostActive()) return true;
#ifndef NATIVE_TESTING
    // The charger holds the cell at ~4.22V; a cell off charge reads
    // noticeably lower within moments of unplugging.
    return DisplayHAL::getBatteryVoltage() >= 4.21f;
#else
    return false;
#endif
}

void recordSleep(SleepReason reason) {
    if (s_sleepRecord.magic != SLEEP_RECORD_MAGIC) {
        s_sleepRecord = SleepRecord();
        s_sleepRecord.magic = SLEEP_RECORD_MAGIC;
    }
    s_sleepRecord.reason = reason;
    s_sleepRecord.usbHost = isUsbHostActive() ? 1 : 0;
    s_sleepRecord.millivolts = (uint16_t)(DisplayHAL::getBatteryVoltage() * 1000.0f);
    s_sleepRecord.idleMs = msSinceActivity();
    s_sleepRecord.count++;
}

const char* lastSleepSummary() {
    static char buf[96];
    if (s_sleepRecord.magic != SLEEP_RECORD_MAGIC || s_sleepRecord.reason == SLEEP_NONE) return "none";
    static const char* kNames[] = {"none", "button", "timeout", "dev-command", "menu"};
    snprintf(buf, sizeof(buf), "%s usbHost=%u %umV idle=%us n=%u",
             kNames[s_sleepRecord.reason < 5 ? s_sleepRecord.reason : 0], s_sleepRecord.usbHost,
             s_sleepRecord.millivolts, (unsigned)(s_sleepRecord.idleMs / 1000), (unsigned)s_sleepRecord.count);
    return buf;
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

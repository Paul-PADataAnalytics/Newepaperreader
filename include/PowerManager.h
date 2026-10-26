#pragma once

#include <cstdint>

// Central owner of the device's awake/asleep policy and power-saving knobs.
//
// Auto-sleep: the device enters deep-sleep "lock mode" after
// SystemSettings::sleepTimeoutMin minutes without user activity (touch, the
// side button, or a user-initiated companion-app command). It never
// auto-sleeps while "Always on" is set, while a computer is connected over
// USB (when "Stay awake on USB" is set), or while a BLE file transfer is in
// progress.
namespace PowerManager {

// Call first thing in setup(): releases GPIO latches set before deep sleep.
void restoreAfterWake();

// Resets the inactivity timer.
void noteUserActivity();

// Keeps the device awake while true (e.g. during a BLE upload).
void setBusy(bool busy);

uint32_t msSinceActivity();
bool shouldAutoSleep();

// True when on external power: a USB host is talking to us (USB
// Serial/JTAG sees SOF packets) or the battery sits at charger voltage
// (covers wall chargers and hosts that suspend the port).
bool isUsbConnected();
bool isUsbHostActive();

// Why the device last went to sleep (kept in RTC memory across deep sleep).
enum SleepReason : uint8_t { SLEEP_NONE = 0, SLEEP_BUTTON, SLEEP_TIMEOUT, SLEEP_DEV_COMMAND, SLEEP_USER_MENU };
void recordSleep(SleepReason reason);
// Human-readable summary of the previous sleep, e.g. for STATUS / HELLO.
const char* lastSleepSummary();

// Full speed for panel drawing (the EPD driver's timing loops assume 240MHz).
void boostCpu();
// Drops to 80MHz once nothing has needed full speed for a little while.
void idleTick();

// Last step before esp_deep_sleep_start(): SD card deselected and held,
// all peripherals owned by DisplayHAL shut down, GPIO latches enabled.
void prepareHardwareForDeepSleep();

} // namespace PowerManager

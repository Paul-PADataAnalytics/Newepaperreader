#pragma once

#include <stdint.h>

enum class LibrarySort {
    AUTHOR,
    GENRE,
    COMPLETION,
    TITLE,
    RECENT
};

// Legacy landscape layout constants (kept only for code that has not been
// ported to the portrait UI kit in src/ui/UIKit.h).
constexpr int LIB_TOP_H = 54;
constexpr int LIB_SIDE_W = 144;
constexpr int LIB_SIDE_X = 960 - LIB_SIDE_W;
constexpr int LIB_MAIN_Y = LIB_TOP_H;
constexpr int LIB_MAIN_W = 960 - LIB_SIDE_W;
constexpr int LIB_MAIN_H = 540 - LIB_TOP_H;

constexpr int LIB_PAGING_W = 96;
constexpr int LIB_LIST_X = LIB_PAGING_W;
constexpr int LIB_LIST_W = LIB_MAIN_W - LIB_PAGING_W;
constexpr int LIB_ROW_H = 80;

class Application {
public:
    virtual ~Application() = default;

    // Lifecycle hooks
    virtual void onCreate() = 0;
    virtual void onDestroy() = 0;

    // UI and events
    virtual void draw() = 0;
    virtual void handleTouch(int x, int y) = 0;
    virtual void update() {}
    // How long the device may sleep before update() needs to run again:
    // 0 keeps the main loop running (background work in progress),
    // UINT32_MAX means update() has nothing time-based to do.
    virtual uint32_t msUntilUpdate() const { return UINT32_MAX; }

    // System back button (the chevron in UI::header, top-left). The Launcher
    // intercepts taps there while wantsSystemBack() is true and calls
    // onBack(); returning false (the default) closes the app and returns
    // to the home screen, returning true means the app navigated within
    // itself (e.g. from a detail view back to its list).
    virtual bool wantsSystemBack() const { return true; }
    virtual bool onBack() { return false; }

    // Text entry: when true, every tap queued while the screen was busy is
    // delivered (none are dropped as stale) and onInputBatchEnd() runs once
    // after each batch, so the app can redraw once for several key presses.
    virtual bool wantsQueuedTaps() const { return false; }
    virtual void onInputBatchEnd() {}

    // Called first when the device is about to deep-sleep (a full reset
    // follows): persist anything the user would not want to lose.
    virtual void onBeforeSleep() {}

    // Called just before deep sleep. Return true after drawing a custom
    // sleep screen (full refresh); false shows the standard one.
    virtual bool drawSleepScreen() { return false; }
    // Seconds until the custom sleep screen should be redrawn (the device
    // briefly wakes on a timer, redraws and sleeps again); 0 = never.
    virtual uint32_t sleepScreenRefreshSec() const { return 0; }
};

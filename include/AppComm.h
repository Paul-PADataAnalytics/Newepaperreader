#pragma once

#include <string>
#include <cstdint>

// Companion-app link (BLE on hardware, a TCP socket on port 9876 in the
// native simulator). See manual/09_power_and_companion.md for the wire protocol.
//
// All incoming messages are queued by the radio task and handled on the
// main loop by poll(), which main.cpp calls once per iteration - apps never
// consume messages themselves.
class AppComm {
public:
    static void init();
    static void deinit();
    static void poll();
    static bool isInitialized();
    static bool isConnected();
    // True while received messages are still queued for poll().
    static bool hasPending();

    // Pushes a single book's progress to the phone (notification).
    static void sendBookDelta(const std::string& isbn, const std::string& title, const std::string& author, int page, int total);

    // Incremented whenever the phone changes the bookmark database, so the
    // eBookmark app knows to redraw.
    static uint32_t bookmarksRevision();

    // Incremented whenever a file is added/removed over the link.
    static uint32_t libraryRevision();

    // Live state for on-device status UI.
    static bool isTransferActive();
    static int transferPercent();
    static std::string transferName();
    static std::string lastEvent();
};

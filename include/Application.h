#pragma once

#include <stdint.h>

enum class LibrarySort {
    AUTHOR,
    GENRE,
    COMPLETION
};

// UI Layout Constants
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
};

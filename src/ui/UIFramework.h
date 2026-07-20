#pragma once

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdint.h>
#endif
#include "DisplayHAL.h"

class UIFramework {
public:
    // Initializes the UI framework (loads fonts, sets up theme constants)
    static void init();

    // Draws the top status bar (battery, title, time)
    static void drawTopBar(uint8_t *framebuffer, const char* title, int batteryPercent);

    // Draws a progress bar at the bottom
    static void drawProgressBar(uint8_t *framebuffer, float progress, const char* label);

    // Draws a button with text centered in it
    static void drawButton(uint8_t *framebuffer, int x, int y, int w, int h, const char* label, bool inverted = false);
    
    // Clears a specific area of the screen
    static void clearArea(uint8_t *framebuffer, int x, int y, int w, int h);
};

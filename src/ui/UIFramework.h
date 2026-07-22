#pragma once

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdint.h>
#endif
#include "DisplayHAL.h"
#include <functional>

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

    // Performs a unified 2-pass localized partial update:
    // Pass 1: Wipes target bounding box (x,y,w,h) to background color & flushes to E-Ink display to reset microspheres.
    // Pass 2: Executes renderContent callback & flushes crisp new content to E-Ink display.
    static void perform2PassPartialUpdate(uint8_t *framebuffer, int x, int y, int w, int h, std::function<void()> renderContent);

    // Draws a 16x16 monotone icon on the screen
    static void drawIcon16x16(uint8_t *framebuffer, int x, int y, const uint8_t *bitmap, uint8_t color);
};

extern const uint8_t COG_ICON[32];

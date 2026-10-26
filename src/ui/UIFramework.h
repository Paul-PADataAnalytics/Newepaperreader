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

    // Draws the top status bar: title on the left, live time/battery on the
    // right (via drawSystemStatus), and a divider line underneath.
    static void drawTopBar(uint8_t *framebuffer, const char* title);

    // Renders the live system status text ("H:MM AM/PM | NN%[+]", or with
    // includeDate=true "Day Mon DD | H:MM AM/PM | NN%[+]") right-aligned so
    // its right edge sits at `rightX`. This is the single source of truth for
    // time/battery display — apps must call this rather than hardcoding
    // placeholder values.
    static void drawSystemStatus(uint8_t *framebuffer, int rightX, int y, float fontSize, uint8_t color, bool includeDate = false);

    // Draws a progress bar at the bottom
    static void drawProgressBar(uint8_t *framebuffer, float progress, const char* label);

    // Draws a button with text centered in it
    static void drawButton(uint8_t *framebuffer, int x, int y, int w, int h, const char* label, bool inverted = false);
    
    // Clears a specific area of the screen to the current background color
    static void clearArea(uint8_t *framebuffer, int x, int y, int w, int h);

    // Returns the current background color (white in light mode, black in dark mode)
    static uint8_t getBackgroundColor();

    // Returns the current foreground color (black in light mode, white in dark mode)
    static uint8_t getForegroundColor();

    // Clears the entire framebuffer to the current background color
    static void clearToBackground(uint8_t *framebuffer);

    // Performs a unified 2-pass localized partial update:
    // Pass 1: Wipes target bounding box (x,y,w,h) to background color & flushes to E-Ink display to reset microspheres.
    // Pass 2: Executes renderContent callback & flushes crisp new content to E-Ink display.
    static void perform2PassPartialUpdate(uint8_t *framebuffer, int x, int y, int w, int h, std::function<void()> renderContent);

    // Performs a unified full-screen draw with mandatory hardware clear:
    // 1. Executes DisplayHAL::clear() to fully reset all E-Ink microspheres (prevents ghosting from previous screen).
    // 2. Clears entire framebuffer to background color.
    // 3. Executes renderContent callback to draw new screen content into framebuffer.
    // 4. Flushes framebuffer to E-Ink display.
    // ALL full-screen redraws MUST go through this method. No raw DisplayHAL::display() calls outside infrastructure.
    static void performFullScreenDraw(uint8_t *framebuffer, std::function<void()> renderContent);

    // Full-screen redraw without the clearing flashes (see
    // DisplayHAL::updateScreenQuick). For page turns.
    static void performQuickFullScreenDraw(uint8_t *framebuffer, std::function<void()> renderContent);

    // Performs a fast partial update
    static void performFastPartialUpdate(uint8_t *framebuffer, int x, int y, int w, int h, std::function<void()> renderContent);

    // Draws a 16x16 monotone icon on the screen
    static void drawIcon16x16(uint8_t *framebuffer, int x, int y, const uint8_t *bitmap, uint8_t color);
};

extern const uint8_t COG_ICON[32];

#pragma once
#include <stdint.h>

#ifndef NATIVE_TESTING
#include "epd_driver.h"
#else
#define EPD_WIDTH 960
#define EPD_HEIGHT 540
#endif

class DisplayHAL {
public:
    static void init();
    static void powerOn();
    static void powerOff();
    static void clear();
    static void display(uint8_t* framebuffer);
    
    static void updateScreenFull();
    static void updateScreenPartial();
    static void updateScreenFast();
    // Whole-screen update without the black/white clearing flashes: erases
    // with a few white pulses, then draws the new frame (~0.8s vs ~1.8s).
    // Used for page turns; leaves faint ghosting, so mix in a regular
    // updateScreenFull() every few pages. Falls back to full in dark mode.
    static void updateScreenQuick();
    
    static uint8_t* frontBuffer;
    static uint8_t* backBuffer;
    
    // Memory
    static uint8_t* allocateFramebuffer();
    static void freeFramebuffer(uint8_t* framebuffer);

    // Graphics primitives
    static void drawHLine(int x, int y, int length, uint8_t color, uint8_t* framebuffer);
    static void drawRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer);
    static void fillRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer);
    static void setPixel(int x, int y, uint8_t color, uint8_t* framebuffer);
    static uint8_t getPixel(int x, int y, uint8_t* framebuffer);

    static void dumpFramebuffer(const char* filepath, uint8_t* framebuffer);
    static bool getTouch(int &x, int &y);

    static void injectTouch(int x, int y);
    
    static void setDebugScreen(bool enable);
    static bool isDebugScreen();
    
    static void setDarkMode(bool enable);
    static bool isDarkMode();

    static void setPortrait(bool portrait);
    static bool isPortrait();
    static int getWidth();
    static int getHeight();

    // Battery & Power Management Hardware Interface
    static float getBatteryVoltage();
    static int getBatteryPercent();
    static bool isCharging();

    // Shuts down every peripheral this HAL owns (EPD rails + config shift
    // register, GT911 touch controller, I2C) and latches the touch INT line
    // low through deep sleep so the controller stays in its sleep mode.
    static void prepareForDeepSleep();

    // Development diagnostics: did the GT911 initialise, and does it
    // still answer on I2C right now?
    static bool touchInitOk();
    static bool touchResponds();

    // Writes what is currently on screen, in the orientation the user sees
    // it, as an 8-bit PGM (width x height header included).
    static void dumpVisible(const char* filepath);

#ifdef NATIVE_TESTING
    static void handleEvents();
    static bool windowShouldClose();
#endif
private:
    static bool s_darkMode;
    static bool s_debugScreen;
};

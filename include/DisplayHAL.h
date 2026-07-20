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
    
    // Memory
    static uint8_t* allocateFramebuffer();
    static void freeFramebuffer(uint8_t* framebuffer);

    // Graphics primitives
    static void drawHLine(int x, int y, int length, uint8_t color, uint8_t* framebuffer);
    static void drawRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer);
    static void fillRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer);

    static void dumpFramebuffer(const char* filepath, uint8_t* framebuffer);
    static bool getTouch(int &x, int &y);

    static void injectTouch(int x, int y);
#ifdef NATIVE_TESTING
    static void handleEvents();
    static bool windowShouldClose();
#endif
};

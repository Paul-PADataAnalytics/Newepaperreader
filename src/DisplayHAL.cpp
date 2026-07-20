#ifndef NATIVE_TESTING
#include "touch.h"
static TouchClass touch;
#endif
#include "DisplayHAL.h"
#include <stdio.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <Wire.h>
#include "utilities.h"
#endif

static int simulatedTouchX = -1;
static int simulatedTouchY = -1;

void DisplayHAL::dumpFramebuffer(const char* filepath, uint8_t* framebuffer) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return;
    // For ESP32, use SPIFFS or SD card if needed, but for native we just use POSIX fopen.
    fprintf(f, "P5\n%d %d\n255\n", EPD_WIDTH, EPD_HEIGHT);
    for (int i = 0; i < EPD_WIDTH * EPD_HEIGHT / 2; i++) {
        uint8_t pair = framebuffer[i];
        uint8_t p1 = (pair & 0xF0) | (pair >> 4);
        uint8_t p2 = ((pair & 0x0F) << 4) | (pair & 0x0F);
        fputc(p1, f);
        fputc(p2, f);
    }
    fclose(f);
}

bool DisplayHAL::getTouch(int &x, int &y) {
    if (simulatedTouchX >= 0 && simulatedTouchY >= 0) {
        x = simulatedTouchX;
        y = simulatedTouchY;
        simulatedTouchX = -1;
        simulatedTouchY = -1;
        return true;
    }
#ifndef NATIVE_TESTING
    if (touch.scanPoint()) {
        uint16_t tx, ty;
        touch.getPoint(tx, ty, 0);
        x = tx;
        y = ty;
        return true;
    }
#endif
    return false;
}

#ifndef NATIVE_TESTING
#include <Arduino.h>


void DisplayHAL::init() {
    epd_init();
    Wire.begin(BOARD_SDA, BOARD_SCL);
    touch.begin(Wire);
}

void DisplayHAL::powerOn() {
    epd_poweron();
}

void DisplayHAL::powerOff() {
    epd_poweroff();
}

void DisplayHAL::clear() {
    epd_clear();
}

void DisplayHAL::display(uint8_t* framebuffer) {
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
}

uint8_t* DisplayHAL::allocateFramebuffer() {
    return (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
}

void DisplayHAL::freeFramebuffer(uint8_t* framebuffer) {
    heap_caps_free(framebuffer);
}

void DisplayHAL::drawHLine(int x, int y, int length, uint8_t color, uint8_t* framebuffer) {
    epd_draw_hline(x, y, length, color, framebuffer);
}

void DisplayHAL::drawRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer) {
    epd_draw_rect(x, y, width, height, color, framebuffer);
}

void DisplayHAL::fillRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer) {
    epd_fill_rect(x, y, width, height, color, framebuffer);
}

#else

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if __has_include(<SDL2/SDL.h>) || __has_include(<SDL.h>)
#define HAS_SDL
#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#endif

#ifdef HAS_SDL
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static bool shouldClose = false;
#endif

void DisplayHAL::init() {
    printf("Native DisplayHAL: init()\n");
#ifdef HAS_SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return;
    }
    window = SDL_CreateWindow("LilyGo-EPD47 Native Mock", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, EPD_WIDTH, EPD_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, EPD_WIDTH, EPD_HEIGHT);
#endif
}

void DisplayHAL::powerOn() {
    printf("Native DisplayHAL: powerOn()\n");
}

void DisplayHAL::powerOff() {
    printf("Native DisplayHAL: powerOff()\n");
}

void DisplayHAL::clear() {
    printf("Native DisplayHAL: clear()\n");
#ifdef HAS_SDL
    if (renderer) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
#endif
}

void DisplayHAL::display(uint8_t* framebuffer) {
    printf("Native DisplayHAL: display() - updating SDL texture\n");
#ifdef HAS_SDL
    if (!texture || !renderer) return;
    
    uint32_t* pixels = new uint32_t[EPD_WIDTH * EPD_HEIGHT];
    for (int i = 0; i < EPD_WIDTH * EPD_HEIGHT / 2; i++) {
        uint8_t pair = framebuffer[i];
        // 0xFF -> white, 0x00 -> black.
        // Assuming 4-bit grayscale, upper nibble = pixel 0, lower nibble = pixel 1? Let's treat byte as 2 pixels.
        // If color was 0x00, both are 0. If 0xFF, both are 255.
        // Let's just expand each nibble to 8-bit.
        uint8_t p1 = (pair & 0xF0) | (pair >> 4);
        uint8_t p2 = ((pair & 0x0F) << 4) | (pair & 0x0F);
        
        // However, the epd_driver often treats 0xFF as white and 0x00 as black.
        pixels[i * 2] = (0xFF << 24) | (p1 << 16) | (p1 << 8) | p1;
        pixels[i * 2 + 1] = (0xFF << 24) | (p2 << 16) | (p2 << 8) | p2;
    }
    
    SDL_UpdateTexture(texture, nullptr, pixels, EPD_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    delete[] pixels;
#endif
}

uint8_t* DisplayHAL::allocateFramebuffer() {
    printf("Native DisplayHAL: allocateFramebuffer()\n");
    return (uint8_t*)malloc(EPD_WIDTH * EPD_HEIGHT / 2);
}

void DisplayHAL::freeFramebuffer(uint8_t* framebuffer) {
    printf("Native DisplayHAL: freeFramebuffer()\n");
    free(framebuffer);
}

// Simple implementations of primitives for the mocked framebuffer
static void setPixel(int x, int y, uint8_t color, uint8_t* framebuffer) {
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    int index = (y * EPD_WIDTH + x) / 2;
    bool isLower = (x % 2 != 0); // Depend on endianness of epd_driver, this is a guess
    
    // We only care about 0xFF (white) and 0x00 (black) for UI elements anyway for now.
    uint8_t c = color & 0x0F;
    if (isLower) {
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    } else {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    }
}

void DisplayHAL::drawHLine(int x, int y, int length, uint8_t color, uint8_t* framebuffer) {
    for (int i = 0; i < length; i++) {
        setPixel(x + i, y, color, framebuffer);
    }
}

void DisplayHAL::drawRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer) {
    for (int i = 0; i < width; i++) {
        setPixel(x + i, y, color, framebuffer);
        setPixel(x + i, y + height - 1, color, framebuffer);
    }
    for (int i = 0; i < height; i++) {
        setPixel(x, y + i, color, framebuffer);
        setPixel(x + width - 1, y + i, color, framebuffer);
    }
}

void DisplayHAL::fillRect(int x, int y, int width, int height, uint8_t color, uint8_t* framebuffer) {
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            setPixel(x + i, y + j, color, framebuffer);
        }
    }
}

void DisplayHAL::handleEvents() {
#ifdef HAS_SDL
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            shouldClose = true;
        }
    }
#endif
}

bool DisplayHAL::windowShouldClose() {
#ifdef HAS_SDL
    return shouldClose;
#else
    return false;
#endif
}

#endif

void DisplayHAL::injectTouch(int x, int y) {
    simulatedTouchX = x;
    simulatedTouchY = y;
}

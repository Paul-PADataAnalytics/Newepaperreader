#ifndef NATIVE_TESTING
#include <TouchDrvGT911.hpp>
static TouchDrvGT911 touch;
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
static bool _isPortrait = false;

bool DisplayHAL::s_darkMode = false;

void DisplayHAL::setDarkMode(bool enable) {
    s_darkMode = enable;
}

bool DisplayHAL::isDarkMode() {
    return s_darkMode;
}

void DisplayHAL::setPortrait(bool portrait) {
    _isPortrait = portrait;
}

bool DisplayHAL::isPortrait() {
    return _isPortrait;
}

int DisplayHAL::getWidth() {
#ifndef NATIVE_TESTING
    return _isPortrait ? 540 : 960;
#else
    return _isPortrait ? EPD_HEIGHT : EPD_WIDTH;
#endif
}

int DisplayHAL::getHeight() {
#ifndef NATIVE_TESTING
    return _isPortrait ? 960 : 540;
#else
    return _isPortrait ? EPD_WIDTH : EPD_HEIGHT;
#endif
}

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
    int tx = -1, ty = -1;
    if (simulatedTouchX >= 0 && simulatedTouchY >= 0) {
        tx = simulatedTouchX;
        ty = simulatedTouchY;
        simulatedTouchX = -1;
        simulatedTouchY = -1;
    } else {
#ifndef NATIVE_TESTING
        int16_t _tx, _ty;
        if (touch.getPoint(&_tx, &_ty, 1)) {
            tx = _tx;
            ty = _ty;
        } else {
            return false;
        }
#else
        return false;
#endif
    }
    
    if (tx >= 0 && ty >= 0) {
        if (_isPortrait) {
#ifndef NATIVE_TESTING
            x = 539 - ty;
            y = tx;
#else
            x = (EPD_HEIGHT - 1) - ty;
            y = tx;
#endif
        } else {
            x = tx;
            y = ty;
        }
        return true;
    }
    return false;
}

#ifndef NATIVE_TESTING
#include <Arduino.h>


void DisplayHAL::init() {
    epd_init();
    
    pinMode(TOUCH_INT, INPUT);
    
    Wire.begin(BOARD_SDA, BOARD_SCL);
    
    uint8_t touchAddress = 0x14;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) touchAddress = 0x14;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) touchAddress = 0x5D;
    
    touch.setPins(-1, TOUCH_INT);
    if (touch.begin(Wire, touchAddress, BOARD_SDA, BOARD_SCL)) {
        touch.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
        touch.setSwapXY(true);
        touch.setMirrorXY(false, true);
    } else {
        printf("Touch initialization failed!\\n");
    }
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
    uint8_t* targetFb = framebuffer;
    uint8_t* invertedFb = nullptr;
    int fbSize = getWidth() * getHeight() / 2;

    if (s_darkMode) {
        invertedFb = (uint8_t*)allocateFramebuffer();
        if (invertedFb) {
            for (int i = 0; i < fbSize; i++) {
                invertedFb[i] = ~framebuffer[i];
            }
            targetFb = invertedFb;
        }
    }

    if (_isPortrait) {
        uint8_t* rotated = allocateFramebuffer();
        if (rotated) {
            memset(rotated, 0xFF, 960 * 540 / 2);
            for (int ly = 0; ly < 960; ly++) {
                for (int lx = 0; lx < 540; lx++) {
                    int l_idx = (ly * 540 + lx) / 2;
                    bool l_isOdd = (lx % 2 != 0);
                    uint8_t color;
                    // Hardware expects even pixels (x=0) in the LOWER nibble, odd in UPPER nibble
                    if (l_isOdd) {
                        color = (targetFb[l_idx] & 0xF0) >> 4;
                    } else {
                        color = targetFb[l_idx] & 0x0F;
                    }
                    
                    int tx = ly;
                    int ty = 539 - lx;
                    int t_idx = (ty * 960 + tx) / 2;
                    bool t_isOdd = (tx % 2 != 0);
                    
                    if (t_isOdd) {
                        rotated[t_idx] = (rotated[t_idx] & 0x0F) | (color << 4);
                    } else {
                        rotated[t_idx] = (rotated[t_idx] & 0xF0) | color;
                    }
                }
            }
            epd_draw_grayscale_image(epd_full_screen(), rotated);
            freeFramebuffer(rotated);
            if (invertedFb) freeFramebuffer(invertedFb);
            return;
        }
    }
    epd_draw_grayscale_image(epd_full_screen(), targetFb);
    if (invertedFb) freeFramebuffer(invertedFb);
}

uint8_t* DisplayHAL::allocateFramebuffer() {
    return (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
}

void DisplayHAL::freeFramebuffer(uint8_t* framebuffer) {
    heap_caps_free(framebuffer);
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
    for (int ly = 0; ly < (_isPortrait ? EPD_WIDTH : EPD_HEIGHT); ly++) {
        for (int lx = 0; lx < (_isPortrait ? EPD_HEIGHT : EPD_WIDTH); lx++) {
            int l_idx = (ly * (_isPortrait ? EPD_HEIGHT : EPD_WIDTH) + lx) / 2;
            bool l_isLower = (lx % 2 != 0);
            uint8_t byteVal = s_darkMode ? ~framebuffer[l_idx] : framebuffer[l_idx];
            uint8_t color;
            if (l_isLower) {
                color = byteVal & 0x0F;
            } else {
                color = (byteVal & 0xF0) >> 4;
            }
            
            // Expand to 8-bit
            uint8_t p = (color << 4) | color;
            uint32_t argb = (0xFF << 24) | (p << 16) | (p << 8) | p;
            
            int tx = _isPortrait ? ly : lx;
            int ty = _isPortrait ? (EPD_HEIGHT - 1 - lx) : ly;
            pixels[ty * EPD_WIDTH + tx] = argb;
        }
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


void DisplayHAL::setPixel(int x, int y, uint8_t color, uint8_t* framebuffer) {
    if (x < 0 || x >= getWidth() || y < 0 || y >= getHeight()) return;
    int index = (y * getWidth() + x) / 2;
    bool isOdd = (x % 2 != 0);
    uint8_t c = color & 0x0F;
#ifndef NATIVE_TESTING
    // EPD47 Hardware expects even pixels (x=0) in the LOWER nibble, and odd pixels (x=1) in the UPPER nibble.
    if (isOdd) {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    } else {
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    }
#else
    // Native mock originally used even pixels in UPPER nibble, odd in LOWER nibble.
    if (isOdd) {
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    } else {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    }
#endif
}

uint8_t DisplayHAL::getPixel(int x, int y, uint8_t* framebuffer) {
    if (x < 0 || x >= getWidth() || y < 0 || y >= getHeight()) return 15;
    int index = (y * getWidth() + x) / 2;
    bool isOdd = (x % 2 != 0);
#ifndef NATIVE_TESTING
    if (isOdd) {
        return (framebuffer[index] & 0xF0) >> 4;
    } else {
        return framebuffer[index] & 0x0F;
    }
#else
    if (isOdd) {
        return framebuffer[index] & 0x0F;
    } else {
        return (framebuffer[index] & 0xF0) >> 4;
    }
#endif
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

void DisplayHAL::injectTouch(int x, int y) {
    simulatedTouchX = x;
    simulatedTouchY = y;
}

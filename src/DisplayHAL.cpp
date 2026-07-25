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
#else
#include <unistd.h>
#endif

static int simulatedTouchX = -1;
static int simulatedTouchY = -1;
static bool _isPortrait = false;

bool DisplayHAL::s_darkMode = false;
bool DisplayHAL::s_debugScreen = false;
uint8_t* DisplayHAL::frontBuffer = nullptr;
uint8_t* DisplayHAL::backBuffer = nullptr;

void DisplayHAL::setDebugScreen(bool enable) {
    s_debugScreen = enable;
}

bool DisplayHAL::isDebugScreen() {
    return s_debugScreen;
}

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
            // In SDL rendering: window pixel (tx, ty) maps to buffer pixel (lx, ly):
            // tx = ly => ly = tx
            // ty = EPD_HEIGHT - 1 - lx => lx = (EPD_HEIGHT - 1) - ty
            // In portrait mode, app width is 540 (EPD_HEIGHT) and app height is 960 (EPD_WIDTH).
            // App coordinate x = lx = (EPD_HEIGHT - 1) - ty
            // App coordinate y = ly = tx
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
    
    pinMode(TOUCH_INT, INPUT_PULLUP);
    gpio_pullup_en(static_cast<gpio_num_t>(TOUCH_INT));
    gpio_pulldown_dis(static_cast<gpio_num_t>(TOUCH_INT));

    // NOTE: GPIO0 must NEVER be touched here (pinMode/pullup/etc) - it is
    // hard-wired on this board to the 74HCT4094D CFG_STR line used
    // internally by the LilyGo-EPD47 driver (epd_poweron/epd_poweroff config
    // shift register strobe). Reconfiguring it as an input (e.g. for a
    // "BOOT button" feature) silently breaks the EPD driver's ability to
    // strobe its config register - code runs with no crash/error, but the
    // panel never visibly updates again. Confirmed via git bisect + GPIO map
    // in .pio/libdeps/*/LilyGo-EPD47/README.MD ("0 | 74HCT4094D CFG_STR | Free: no").
    
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

float DisplayHAL::getBatteryVoltage() {
    // LilyGo T5 EPD47 S3 battery ADC voltage divider configuration
    // Battery ADC pin is GPIO 14 (or BATT_PIN / BATT_ADC_PIN depending on board revision)
    #ifndef BATT_PIN
    #define BATT_PIN 14
    #endif
    
    // Read raw ADC value (12-bit ADC: 0..4095)
    uint32_t raw = analogRead(BATT_PIN);
    
    // Voltage divider: 2 * (raw / 4095.0) * 3.3V (with 1.1V attenuation factor / divider)
    float voltage = (float)raw / 4095.0f * 2.0f * 3.3f * 1.1f;
    return voltage;
}

int DisplayHAL::getBatteryPercent() {
    float v = getBatteryVoltage();
    // 820mAh LiPo cell discharge curve mapping: 4.2V = 100%, 3.3V = 0%
    if (v >= 4.2f) return 100;
    if (v <= 3.3f) return 0;
    int pct = (int)(((v - 3.3f) / (4.2f - 3.3f)) * 100.0f);
    return pct;
}

bool DisplayHAL::isCharging() {
    // Returns true if battery voltage is at/near charging cutoff threshold (>= 4.25V)
    return getBatteryVoltage() >= 4.25f;
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

#ifdef HAS_SDL
#include <SDL2/SDL.h>
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

float DisplayHAL::getBatteryVoltage() {
    return 4.15f; // Mock nominal 4.15V LiPo battery
}

int DisplayHAL::getBatteryPercent() {
    return 95; // Mock 95% battery capacity
}

bool DisplayHAL::isCharging() {
    return false; // Mock not charging
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
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            simulatedTouchX = e.button.x;
            simulatedTouchY = e.button.y;
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

void DisplayHAL::updateScreenFull() {
    if (!frontBuffer || !backBuffer) return;
    
#ifdef NATIVE_TESTING
    if (s_debugScreen) {
        printf("[DEBUG-SCREEN] updateScreenFull() requested.\n");
        uint8_t* temp = allocateFramebuffer();
        memset(temp, 0x00, getWidth() * getHeight() / 2); // Black flash
        display(temp);
        usleep(150000);
        memset(temp, 0xFF, getWidth() * getHeight() / 2); // White flash
        display(temp);
        usleep(150000);
        freeFramebuffer(temp);
    }
#endif

    clear();
    display(frontBuffer);
    memcpy(backBuffer, frontBuffer, getWidth() * getHeight() / 2);
}

void DisplayHAL::updateScreenPartial() {
    if (!frontBuffer || !backBuffer) return;
    int w = getWidth();
    int h = getHeight();
    
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (getPixel(x, y, frontBuffer) != getPixel(x, y, backBuffer)) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < minX || maxY < minY) {
#ifdef NATIVE_TESTING
        if (s_debugScreen) {
            printf("[DEBUG-SCREEN] updateScreenPartial() ignored (no changes).\n");
        }
#endif
        return; 
    }

#ifdef NATIVE_TESTING
    if (s_debugScreen) {
        printf("[DEBUG-SCREEN] updateScreenPartial() Diff Bounds: x=%d, y=%d, w=%d, h=%d\n", 
               minX, minY, (maxX - minX + 1), (maxY - minY + 1));
    }
#endif
    
#ifdef NATIVE_TESTING
    uint8_t* tempFb = allocateFramebuffer();
    if (tempFb) {
        memcpy(tempFb, backBuffer, w * h / 2);
        fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0xFF, tempFb);
        
        if (s_debugScreen) {
            // Flash bounded clear region to white
            display(tempFb);
            usleep(100000);
            // Flash it black to highlight the bounding box
            fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0x00, tempFb);
            display(tempFb);
            usleep(100000);
            // Flash back to white
            fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0xFF, tempFb);
            display(tempFb);
            usleep(100000);
        } else {
            display(tempFb);
        }
        freeFramebuffer(tempFb);
    }
#else
    // Hardware E-ink physical clear waveform for the bounding box area
    int physical_minX, physical_minY, physical_w, physical_h;
    if (_isPortrait) {
        physical_minX = minY;
        physical_minY = EPD_HEIGHT - 1 - maxX;
        physical_w = (maxY - minY + 1);
        physical_h = (maxX - minX + 1);
    } else {
        physical_minX = minX;
        physical_minY = minY;
        physical_w = (maxX - minX + 1);
        physical_h = (maxY - minY + 1);
    }

    Rect_t area = { 
        .x = physical_minX, 
        .y = physical_minY, 
        .width = physical_w, 
        .height = physical_h 
    };
    epd_clear_area(area);
#endif

    display(frontBuffer);
    
    memcpy(backBuffer, frontBuffer, w * h / 2);
}

void DisplayHAL::updateScreenFast() {
    if (!frontBuffer || !backBuffer) return;
    int w = getWidth();
    int h = getHeight();
    
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (getPixel(x, y, frontBuffer) != getPixel(x, y, backBuffer)) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < minX || maxY < minY) return; 
    
    int physical_minX, physical_minY, physical_w, physical_h;
    if (_isPortrait) {
        physical_minX = minY;
        physical_minY = EPD_HEIGHT - 1 - maxX;
        physical_w = (maxY - minY + 1);
        physical_h = (maxX - minX + 1);
    } else {
        physical_minX = minX;
        physical_minY = minY;
        physical_w = (maxX - minX + 1);
        physical_h = (maxY - minY + 1);
    }
    
#ifndef NATIVE_TESTING
    // Align physical_minX to 16-pixel boundary to avoid epd_draw_frame_1bit shifting bugs
    int original_maxX = physical_minX + physical_w;
    physical_minX = physical_minX & ~15;
    physical_w = original_maxX - physical_minX;
    if (physical_w % 16 != 0) {
        physical_w = (physical_w + 15) & ~15;
    }

    Rect_t area = { 
        .x = physical_minX, 
        .y = physical_minY, 
        .width = physical_w, 
        .height = physical_h 
    };

    // Hardware 1-bit fast update
    int stride = physical_w / 8;
    int monoStride = stride;
    uint8_t* monoBuf = (uint8_t*)calloc(1, monoStride * physical_h);
    if (monoBuf) {
        for (int py = 0; py < physical_h; py++) {
            for (int px = 0; px < physical_w; px++) {
                int physical_abs_x = physical_minX + px;
                int physical_abs_y = physical_minY + py;
                
                int lx, ly;
                if (_isPortrait) {
                    ly = physical_abs_x;
                    lx = EPD_HEIGHT - 1 - physical_abs_y;
                } else {
                    lx = physical_abs_x;
                    ly = physical_abs_y;
                }

                if (lx < 0 || lx >= DisplayHAL::getWidth() || ly < 0 || ly >= DisplayHAL::getHeight()) {
                    continue;
                }

                uint8_t color = getPixel(lx, ly, frontBuffer);
                // 1bpp driver ONLY draws black pixels and skips 0s. 
                // We want to draw the dark pixels as black.
                bool isBlack = (color < 0x08);
                if (isBlack) {
                    monoBuf[py * stride + (px / 8)] |= (1 << (px % 8));
                }
            }
        }

        // FIX: The LilyGo calc_epd_input_1bpp has a byte-swap bug!
        // It maps line_data[0] to the upper 16-bits of the 32-bit DMA word, and line_data[1] to the lower 16-bits.
        // Because ESP32 is little-endian, the DMA transmits the lower 16-bits FIRST.
        // So line_data[1] (pixels 8-15) is drawn BEFORE line_data[0] (pixels 0-7)!
        // We must swap every adjacent pair of bytes in monoBuf to compensate.
        for (int i = 0; i < monoStride * physical_h; i += 2) {
            if (i + 1 < monoStride * physical_h) {
                uint8_t temp = monoBuf[i];
                monoBuf[i] = monoBuf[i + 1];
                monoBuf[i + 1] = temp;
            }
        }

        // Cleanly erase the bounding box area with multiple white pulses.
        // Pulsing is required for E-ink to overcome particle inertia.
        // 8 pulses of 500 ticks (50 * 10) = 4000 ticks of white to fully erase deep black.
        for (int i = 0; i < 8; i++) {
            epd_push_pixels(area, 50, 1);
        }

        // Push 1-bit frame of black pixels in multiple pulses.
        // 4 pulses of 500 ticks = 2000 ticks for deep, high-contrast black.
        for (int i = 0; i < 4; i++) {
            epd_draw_frame_1bit(area, monoBuf, BLACK_ON_WHITE, 500);
        }

        free(monoBuf);
    }
#else
    if (s_debugScreen) {
        printf("[DEBUG-SCREEN] updateScreenFast() Fast Mono Bounding Box: x=%d, y=%d, w=%d, h=%d\n", 
               minX, minY, (maxX - minX + 1), (maxY - minY + 1));
        // Visually flash it very quickly
        uint8_t* tempFb = allocateFramebuffer();
        if (tempFb) {
            memcpy(tempFb, backBuffer, w * h / 2);
            fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0x00, tempFb);
            display(tempFb);
            usleep(20000); // 20ms
            freeFramebuffer(tempFb);
        }
    }
    display(frontBuffer);
#endif

    memcpy(backBuffer, frontBuffer, w * h / 2);
}

void DisplayHAL::injectTouch(int x, int y) {
    simulatedTouchX = x;
    simulatedTouchY = y;
}

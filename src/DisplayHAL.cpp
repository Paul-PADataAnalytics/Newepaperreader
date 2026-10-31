#ifndef NATIVE_TESTING
#include <TouchDrvGT911.hpp>
static TouchDrvGT911 touch;
#endif
#include "DisplayHAL.h"
#include <stdio.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include "utilities.h"
#include "PowerManager.h"
#include "i2s_data_bus.h" // i2s_is_busy() for clearPhysicalArea()
#include "ed047tc1.h"
#else
#include <unistd.h>
#include <string.h>
#endif

static int simulatedTouchX = -1;
static int simulatedTouchY = -1;
static bool _isPortrait = false;
static bool s_hasShownFrame = false;
static bool s_shownPortrait = false;
static bool s_shownDark = false;

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

// The panel's high-voltage rails (and the battery sense divider, which sits
// behind the same POWER_EN switch) are only switched on while the panel is
// actually being driven. They used to stay on for the whole time the device
// was awake, which kept the boost converters running for no reason.
static int s_epdPowerDepth = 0;
static float s_battVoltage = -1.0f;
static uint32_t s_battSampleMs = 0;
static const uint32_t BATTERY_SAMPLE_INTERVAL_MS = 60000;

static void sampleBatteryWhilePowered() {
    uint32_t mv = analogReadMilliVolts(BATT_PIN);
    s_battVoltage = (mv * 2) / 1000.0f; // 1:2 divider
    s_battSampleMs = millis();
    if (s_battSampleMs == 0) s_battSampleMs = 1;
}

struct EpdPowerScope {
    EpdPowerScope() {
        if (s_epdPowerDepth++ == 0) {
            PowerManager::boostCpu();
            epd_poweron();
        }
    }
    ~EpdPowerScope() {
        if (--s_epdPowerDepth == 0) {
            if (s_battSampleMs == 0 || millis() - s_battSampleMs > BATTERY_SAMPLE_INTERVAL_MS) {
                sampleBatteryWhilePowered();
            }
            epd_poweroff();
        }
    }
};
#define EPD_POWER_SCOPE() EpdPowerScope _epdPowerScope

static bool s_touchOk = false;
static uint8_t s_touchAddress = 0x5D;

void DisplayHAL::init() {
    epd_init();

    // NOTE: GPIO0 must NEVER be touched here (pinMode/pullup/etc) - it is
    // hard-wired on this board to the 74HCT4094D CFG_STR line used
    // internally by the LilyGo-EPD47 driver (epd_poweron/epd_poweroff config
    // shift register strobe). Reconfiguring it as an input (e.g. for a
    // "BOOT button" feature) silently breaks the EPD driver's ability to
    // strobe its config register - code runs with no crash/error, but the
    // panel never visibly updates again. Confirmed via git bisect + GPIO map
    // in .pio/libdeps/*/LilyGo-EPD47/README.MD ("0 | 74HCT4094D CFG_STR | Free: no").

    // The GT911 is put into its sleep mode before deep sleep (see
    // prepareForDeepSleep()); a high pulse on INT wakes it back up. Harmless
    // when it was already awake (cold boot).
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);
    delay(10);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    gpio_pullup_en(static_cast<gpio_num_t>(TOUCH_INT));
    gpio_pulldown_dis(static_cast<gpio_num_t>(TOUCH_INT));

    Wire.begin(BOARD_SDA, BOARD_SCL);

    uint8_t touchAddress = 0x14;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) touchAddress = 0x14;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) touchAddress = 0x5D;

    touch.setPins(-1, TOUCH_INT);
    s_touchAddress = touchAddress;
    s_touchOk = touch.begin(Wire, touchAddress, BOARD_SDA, BOARD_SCL);
    Serial.printf("GT911 at 0x%02X init %s\n", touchAddress, s_touchOk ? "ok" : "FAILED");
    if (s_touchOk) {
        touch.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
        touch.setSwapXY(true);
        touch.setMirrorXY(false, true);
    } else {
        printf("Touch initialization failed!\n");
    }
    // touch.begin() may have reconfigured INT; restore the pull-up input.
    pinMode(TOUCH_INT, INPUT_PULLUP);
}

bool DisplayHAL::touchInitOk() { return s_touchOk; }

bool DisplayHAL::touchResponds() {
    Wire.beginTransmission(s_touchAddress);
    return Wire.endTransmission() == 0;
}

void DisplayHAL::powerOn() {
    // Rails are now switched automatically around every panel operation.
}

void DisplayHAL::powerOff() {
    if (s_epdPowerDepth == 0) epd_poweroff();
}

void DisplayHAL::prepareForDeepSleep() {
    gpio_intr_disable(static_cast<gpio_num_t>(TOUCH_INT));
    // Zero the whole EPD config register (not just the HV rails), as the
    // LilyGo deep-sleep reference does - this is what gets the board down
    // to its ~0.4mA sleep figure.
    epd_poweroff_all();
    s_epdPowerDepth = 0;
    // Command the GT911 into sleep (INT is driven low by sleep()); it would
    // otherwise keep scanning the panel at several mA all night.
    touch.sleep();
    delay(20);
    Wire.end();
    gpio_hold_en(static_cast<gpio_num_t>(TOUCH_INT));
}

void DisplayHAL::dumpVisible(const char* filepath) {
    (void)filepath;
}

float DisplayHAL::getBatteryVoltage() {
    if (s_battSampleMs == 0 || millis() - s_battSampleMs > BATTERY_SAMPLE_INTERVAL_MS * 5) {
        EPD_POWER_SCOPE();
        delay(10); // let the divider settle after POWER_EN switches on
        sampleBatteryWhilePowered();
    }
    return s_battVoltage;
}

int DisplayHAL::getBatteryPercent() {
    // Typical single-cell LiPo resting discharge curve.
    static const float kVolts[] = {4.20f, 4.10f, 4.00f, 3.92f, 3.87f, 3.82f, 3.79f, 3.77f, 3.74f, 3.68f, 3.45f, 3.30f};
    static const int   kPct[]   = {100,   90,    80,    70,    60,    50,    40,    30,    20,    10,    5,     0};
    float v = getBatteryVoltage();
    if (v >= kVolts[0]) return 100;
    for (int i = 1; i < 12; i++) {
        if (v >= kVolts[i]) {
            float t = (v - kVolts[i]) / (kVolts[i - 1] - kVolts[i]);
            return kPct[i] + (int)(t * (kPct[i - 1] - kPct[i]) + 0.5f);
        }
    }
    return 0;
}

bool DisplayHAL::isCharging() {
    return PowerManager::isUsbConnected() || getBatteryVoltage() >= 4.25f;
}


void DisplayHAL::clear() {
    EPD_POWER_SCOPE();
    epd_clear();
}


static void displayWithMode(uint8_t* framebuffer, DrawMode_t mode);

void DisplayHAL::display(uint8_t* framebuffer) {
    displayWithMode(framebuffer, BLACK_ON_WHITE);
}

static void displayWithMode(uint8_t* framebuffer, DrawMode_t mode) {
    EPD_POWER_SCOPE();
    uint8_t* targetFb = framebuffer;
    uint8_t* invertedFb = nullptr;
    int fbSize = EPD_WIDTH * EPD_HEIGHT / 2;

    if (DisplayHAL::isDarkMode()) {
        invertedFb = DisplayHAL::allocateFramebuffer();
        if (invertedFb) {
            for (int i = 0; i < fbSize; i++) {
                invertedFb[i] = ~framebuffer[i];
            }
            targetFb = invertedFb;
        }
    }

    if (_isPortrait) {
        uint8_t* rotated = DisplayHAL::allocateFramebuffer();
        if (rotated) {
            // Logical portrait (lx, ly) maps to physical (tx = ly, ty = 539 - lx).
            // Walk two logical rows at a time so every physical output byte
            // (two horizontally adjacent physical pixels = two adjacent
            // logical rows) is written exactly once.
            for (int ly = 0; ly < 960; ly += 2) {
                const uint8_t* rowA = targetFb + (ly * 540) / 2;
                const uint8_t* rowB = targetFb + ((ly + 1) * 540) / 2;
                int tx = ly; // even
                for (int lx = 0; lx < 540; lx++) {
                    uint8_t a = (lx & 1) ? (rowA[lx >> 1] >> 4) : (rowA[lx >> 1] & 0x0F);
                    uint8_t b = (lx & 1) ? (rowB[lx >> 1] >> 4) : (rowB[lx >> 1] & 0x0F);
                    int ty = 539 - lx;
                    // Hardware expects even pixels in the LOWER nibble.
                    rotated[(ty * 960 + tx) >> 1] = (uint8_t)((b << 4) | a);
                }
            }
            epd_draw_image(epd_full_screen(), rotated, mode);
            DisplayHAL::freeFramebuffer(rotated);
            if (invertedFb) DisplayHAL::freeFramebuffer(invertedFb);
            return;
        }
    }
    epd_draw_image(epd_full_screen(), targetFb, mode);
    if (invertedFb) DisplayHAL::freeFramebuffer(invertedFb);
}

uint8_t* DisplayHAL::allocateFramebuffer() {
    return (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
}

// White pulses used to erase the previous page in updateScreenQuick():
// 4 (~140ms) leaves only a very faint ghost on this panel, cleaned up by the
// periodic full refresh. Redrawing the old frame with WHITE_ON_WHITE instead
// was tried: no cleaner, and it costs a full 15-frame draw (~640ms).
static const int QUICK_ERASE_PULSES = 4;

void DisplayHAL::updateScreenQuick() {
    if (!frontBuffer || !backBuffer) return;
    if (s_darkMode) {
        // The white-ink erase below assumes a white page.
        updateScreenFull();
        return;
    }
    EPD_POWER_SCOPE();
    uint32_t t0 = millis();
    // Erase with a few white-only pulses (no black flash), then draw the new
    // frame. Faint ghosting builds up; callers do a full updateScreenFull()
    // every few pages.
    for (int i = 0; i < QUICK_ERASE_PULSES; i++) epd_push_pixels(epd_full_screen(), 50, 1);
    uint32_t t1 = millis();
    displayWithMode(frontBuffer, BLACK_ON_WHITE);
    memcpy(backBuffer, frontBuffer, EPD_WIDTH * EPD_HEIGHT / 2);
    Serial.printf("EPD quick: erase %ums draw %ums\n", (unsigned)(t1 - t0), (unsigned)(millis() - t1));
}

void DisplayHAL::freeFramebuffer(uint8_t* framebuffer) {
    heap_caps_free(framebuffer);
}




#else

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EPD_POWER_SCOPE() do {} while (0)

#ifdef HAS_SDL
#include <SDL2/SDL.h>
#endif

// The simulator window is portrait (540 x 960), matching how the device is
// held. Every frame is converted into this 8-bit "visible" image, which is
// also what dumpVisible() writes out for automated screenshots.
static const int VIS_W = EPD_HEIGHT;
static const int VIS_H = EPD_WIDTH;
static uint8_t s_visible[EPD_WIDTH * EPD_HEIGHT];

#ifdef HAS_SDL
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static bool shouldClose = false;
#endif

void DisplayHAL::init() {
    printf("Native DisplayHAL: init()\n");
    memset(s_visible, 0xFF, sizeof(s_visible));
#ifdef HAS_SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return;
    }
    window = SDL_CreateWindow("LilyGo-EPD47 Native Mock", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, VIS_W, VIS_H, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VIS_W, VIS_H);
#endif
}

void DisplayHAL::powerOn() {
}

void DisplayHAL::powerOff() {
}

void DisplayHAL::prepareForDeepSleep() {
    printf("Native DisplayHAL: prepareForDeepSleep()\n");
}

bool DisplayHAL::touchInitOk() { return true; }
bool DisplayHAL::touchResponds() { return true; }

void DisplayHAL::dumpVisible(const char* filepath) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return;
    fprintf(f, "P5\n%d %d\n255\n", VIS_W, VIS_H);
    fwrite(s_visible, 1, sizeof(s_visible), f);
    fclose(f);
}

float DisplayHAL::getBatteryVoltage() {
    return 4.05f; // Mock LiPo voltage
}

int DisplayHAL::getBatteryPercent() {
    return 85; // Mock battery capacity
}

bool DisplayHAL::isCharging() {
    return false; // Mock not charging
}

static void presentVisible() {
#ifdef HAS_SDL
    if (!texture || !renderer) return;
    uint32_t* pixels = new uint32_t[VIS_W * VIS_H];
    for (int i = 0; i < VIS_W * VIS_H; i++) {
        uint8_t p = s_visible[i];
        pixels[i] = (0xFFu << 24) | (p << 16) | (p << 8) | p;
    }
    SDL_UpdateTexture(texture, nullptr, pixels, VIS_W * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    delete[] pixels;
#endif
}

void DisplayHAL::clear() {
    memset(s_visible, 0xFF, sizeof(s_visible));
    presentVisible();
}

void DisplayHAL::display(uint8_t* framebuffer) {
    int lw = _isPortrait ? EPD_HEIGHT : EPD_WIDTH;
    int lh = _isPortrait ? EPD_WIDTH : EPD_HEIGHT;
    for (int ly = 0; ly < lh; ly++) {
        for (int lx = 0; lx < lw; lx++) {
            int l_idx = (ly * lw + lx) / 2;
            uint8_t byteVal = s_darkMode ? (uint8_t)~framebuffer[l_idx] : framebuffer[l_idx];
            // Native mock packs even pixels in the UPPER nibble.
            uint8_t color = (lx % 2 != 0) ? (byteVal & 0x0F) : ((byteVal & 0xF0) >> 4);
            int vx, vy;
            if (_isPortrait) {
                vx = lx;
                vy = ly;
            } else {
                // Physical landscape buffer seen on a portrait-held panel.
                vx = (EPD_HEIGHT - 1) - ly;
                vy = lx;
            }
            s_visible[vy * VIS_W + vx] = (uint8_t)((color << 4) | color);
        }
    }
    presentVisible();
}

uint8_t* DisplayHAL::allocateFramebuffer() {
    return (uint8_t*)malloc(EPD_WIDTH * EPD_HEIGHT / 2);
}

void DisplayHAL::updateScreenQuick() {
    if (!frontBuffer || !backBuffer) return;
    display(frontBuffer);
    memcpy(backBuffer, frontBuffer, EPD_WIDTH * EPD_HEIGHT / 2);
}

void DisplayHAL::freeFramebuffer(uint8_t* framebuffer) {
    free(framebuffer);
}

void DisplayHAL::handleEvents() {
#ifdef HAS_SDL
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            shouldClose = true;
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            // Window is portrait; convert to the raw (landscape) touch
            // controller coordinates that getTouch() expects, exactly like
            // the real GT911 reports them.
            simulatedTouchX = e.button.y;
            simulatedTouchY = (EPD_HEIGHT - 1) - e.button.x;
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
    EPD_POWER_SCOPE();
    
#ifdef NATIVE_TESTING
    if (s_debugScreen) {
        printf("[DEBUG-SCREEN] updateScreenFull() requested.\n");
        uint8_t* temp = allocateFramebuffer();
        if (temp) {
            memset(temp, 0x00, getWidth() * getHeight() / 2);
            display(temp);
            memset(temp, 0xFF, getWidth() * getHeight() / 2);
            display(temp);
            freeFramebuffer(temp);
        }
    }
#endif

#ifndef NATIVE_TESTING
    uint32_t t0 = millis();
    clear();
    uint32_t t1 = millis();
    display(frontBuffer);
    Serial.printf("EPD full: clear %ums draw %ums\n", (unsigned)(t1 - t0), (unsigned)(millis() - t1));
#else
    clear();
    display(frontBuffer);
#endif
    memcpy(backBuffer, frontBuffer, getWidth() * getHeight() / 2);
    s_hasShownFrame = true;
    s_shownPortrait = _isPortrait;
    s_shownDark = s_darkMode;
}

// The EPD driver's partial-area path mishandles regions that start on an
// odd column or have an odd width: its in-place nibble shift carries the
// previous row's pixels into the columns just past the region, which showed
// up as a dark band along the bottom of the home-screen clock (in portrait
// the physical x axis runs down the screen). Regions are snapped to whole
// 16-pixel groups (see clearPhysicalArea() for why 8 was not enough).
static const int PHYSICAL_ALIGN = 16;

static void alignPhysicalSpan(int& x, int& w) {
    int start = x & ~(PHYSICAL_ALIGN - 1);
    int end = (x + w + PHYSICAL_ALIGN - 1) & ~(PHYSICAL_ALIGN - 1);
    if (end > EPD_WIDTH) end = EPD_WIDTH;
    x = start;
    w = end - start;
}

#ifndef NATIVE_TESTING
// Zeroes the line buffer once the bus has finished sending it (the S3 build
// of the driver has a single line buffer that the DMA reads in place).
static void clearLineBuffer() {
    while (i2s_is_busy()) {}
    memset((void*)epd_get_current_buffer(), 0, EPD_WIDTH / 4);
}

// One push of white (or black) over a physical area. Replaces the driver's
// epd_push_pixels()/epd_clear_area() for partial regions, which on this
// board:
//  - swaps the two 8-pixel halves of every 16-pixel group (its
//    reorder_line_buffer() suits the original ESP32's I2S bus, but the S3's
//    LCD peripheral sends bytes in order). An area edge on an 8-but-not-16
//    boundary was therefore never erased, while 8 pixels just outside it
//    were flashed white and never redrawn - the stripes left on settings
//    toggles and around the reading menu.
//  - latched the area's last row with the 5us "skip" time instead of the
//    full push time, and zeroed the line buffer while that row was still
//    being sent, so the row was barely erased.
// Regions are 16-aligned as well, so either byte order gives the same result.
static void pushPhysicalArea(const Rect_t& area, int time, bool white) {
    uint8_t row[EPD_WIDTH / 4];
    memset(row, 0, sizeof(row));
    const uint8_t pattern = white ? 0xAA : 0x55;
    for (int x = area.x; x < area.x + area.width; x++) {
        row[x >> 2] |= pattern & (0x03 << (2 * (x & 3)));
    }
    const int end = area.y + area.height;

    epd_start_frame();
    int skipped = 0;
    bool driven = false;
    for (int i = 0; i < EPD_HEIGHT; i++) {
        if (i >= area.y && i < end) {
            if (i == area.y) {
                while (i2s_is_busy()) {}
                memcpy((void*)epd_get_current_buffer(), row, sizeof(row));
            }
            epd_output_row(time * 10);
            driven = true;
            skipped = 0;
        } else if (skipped == 0) {
            // Latches the previous (area) row at full strength, then sends
            // a no-op row.
            clearLineBuffer();
            epd_output_row(driven ? time * 10 : time);
            skipped++;
        } else if (skipped == 1) {
            epd_output_row(10);
            skipped++;
        } else {
            epd_skip();
        }
    }
    // Leave no-op data latched: the driver's next frame may clock rows
    // with epd_skip(), which drives whatever is still latched.
    if (skipped == 0) {
        clearLineBuffer();
        epd_output_row(time * 10);
        epd_output_row(10);
    } else if (skipped == 1) {
        epd_output_row(10);
    }
    epd_end_frame();
}

// Same black/white cycling as the driver's epd_clear_area().
static void clearPhysicalArea(const Rect_t& area) {
    const int cycles = 4, time = 50;
    for (int c = 0; c < cycles; c++) {
        for (int i = 0; i < 4; i++) pushPhysicalArea(area, time, false);
        for (int i = 0; i < 4; i++) pushPhysicalArea(area, time, true);
    }
}
#endif

void DisplayHAL::updateScreenPartial() {
    if (!frontBuffer || !backBuffer) return;
    if (!s_hasShownFrame || s_shownPortrait != _isPortrait || s_shownDark != s_darkMode) {
        updateScreenFull();
        return;
    }
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
    EPD_POWER_SCOPE();

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
            // NATIVE_SLEEP_DISABLED;
            // Flash it black to highlight the bounding box
            fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0x00, tempFb);
            display(tempFb);
            // NATIVE_SLEEP_DISABLED;
            // Flash back to white
            fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, 0xFF, tempFb);
            display(tempFb);
            // NATIVE_SLEEP_DISABLED;
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
    alignPhysicalSpan(physical_minX, physical_w);

    Rect_t area = { 
        .x = physical_minX, 
        .y = physical_minY, 
        .width = physical_w, 
        .height = physical_h 
    };
    const int stride = area.width / 2;
    // When the area stops short of the panel's last row, the driver zeroes
    // its line buffer while the area's last row is still being sent, so that
    // row would mostly not be drawn. Draw one extra all-white (no-op) row to
    // absorb that.
    const bool padRow = area.y + area.height < EPD_HEIGHT;
    Rect_t drawArea = area;
    if (padRow) drawArea.height++;
    uint8_t* region = static_cast<uint8_t*>(malloc(stride * drawArea.height));
    if (!region) {
        Serial.println("[display] partial allocation failed; full refresh");
        updateScreenFull();
        return;
    }
    for (int py = 0; py < area.height; ++py) {
        for (int px = 0; px < area.width; px += 2) {
            const int ax = area.x + px;
            const int ay = area.y + py;
            const int lx = _isPortrait ? EPD_HEIGHT - 1 - ay : ax;
            const int ly = _isPortrait ? ax : ay;
            uint8_t a = getPixel(lx, ly, frontBuffer);
            uint8_t b = getPixel(_isPortrait ? lx : ax + 1,
                                 _isPortrait ? ax + 1 : ay, frontBuffer);
            if (s_darkMode) {
                a = 15 - a;
                b = 15 - b;
            }
            region[py * stride + px / 2] = a | (b << 4);
        }
    }
    if (padRow) memset(region + stride * area.height, 0xFF, stride);
    uint32_t t0 = millis();
    clearPhysicalArea(area);
    uint32_t t1 = millis();
    epd_draw_grayscale_image(drawArea, region);
    free(region);
    Serial.printf("EPD partial %dx%d@%d,%d: clear %ums draw %ums\n", area.width, area.height, area.x, area.y,
                  (unsigned)(t1 - t0), (unsigned)(millis() - t1));
#endif

#ifdef NATIVE_TESTING
    display(frontBuffer);
#endif
    
    memcpy(backBuffer, frontBuffer, w * h / 2);
}

void DisplayHAL::updateScreenFast() {
    // UI updates need a complete region erase; the former one-cycle path
    // left gray text and inverted toggle edges behind.
    updateScreenPartial();
}

void DisplayHAL::injectTouch(int x, int y) {
    simulatedTouchX = x;
    simulatedTouchY = y;
}

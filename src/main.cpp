#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
#include "utilities.h"
#else
#include <stdio.h>
#include <unistd.h>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iostream>
#endif

#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include "Launcher.h"
#include "AppComm.h"

#include "embedded_font.h"

uint8_t *framebuffer;
TypographyEngine typography;

#ifdef NATIVE_TESTING
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
#endif

static unsigned long lastTouchTime = 0;

void exitToSystemLauncher() {
    Launcher::getInstance().exitCurrentApp();
}

void launchSettingsApp() {
    Launcher::getInstance().switchToApp(2);
}

void processSerialCommands() {
#ifndef NATIVE_TESTING
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.startsWith("T ")) {
            int firstSpace = cmd.indexOf(' ');
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            if (secondSpace != -1) {
                int x = cmd.substring(firstSpace + 1, secondSpace).toInt();
                int y = cmd.substring(secondSpace + 1).toInt();
                DisplayHAL::injectTouch(x, y);
            }
        } else if (cmd == "S") {
            Serial.println("SCREENSHOT_START");
            Serial.println("P5");
            Serial.println("960 540");
            Serial.println("255");
            Serial.write(framebuffer, 960 * 540 / 2);
            Serial.println();
            Serial.println("SCREENSHOT_END");
        }
    }
#endif
}

void setup() {
#ifndef NATIVE_TESTING
    Serial.begin(115200);
    Serial.println("Starting LilyGO EPD47 E-Reader System v1.02a...");
#else
    printf("Starting LilyGO EPD47 E-Reader System v1.02a (Native Mock)...\n");
#endif

    DisplayHAL::init();
    framebuffer = DisplayHAL::allocateFramebuffer();
    if (!framebuffer) {
#ifndef NATIVE_TESTING
        Serial.println("Failed to allocate framebuffer!");
#else
        printf("Failed to allocate framebuffer!\n");
#endif
        return;
    }

    memset(framebuffer, 0xFF, 960 * 540 / 2);
    DisplayHAL::powerOn();
    DisplayHAL::clear();
    
    AppStorage::initialize();
    UIFramework::init();
    
    // Load default font for system launcher and apps
#ifndef NATIVE_TESTING
    if (!typography.loadFont("/sd/data/Roboto-Regular.ttf", 36.0f)) {
        typography.loadFontFromMemory(data_Roboto_Regular_ttf, data_Roboto_Regular_ttf_len, 36.0f);
    }
#else
    if (!typography.loadFont("data/Roboto-Regular.ttf", 32)) {
        printf("Failed to load Roboto-Regular.ttf\n");
    }
#endif

    // Initialize App Launcher
    Launcher::getInstance().init();
    
    // Start BLE background system service continuously on system startup
    AppComm::init();

    Launcher::getInstance().drawMenu();
}

static uint32_t lastTouchActivityTime = 0;
static bool isSystemSleeping = false;

static void drawSleepScreen() {
    bool wasPortrait = DisplayHAL::isPortrait();
    DisplayHAL::setPortrait(false); // Sleep screen is rendered landscape
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    // 1. Hardware E-Ink clear to prevent screen burn-in
    DisplayHAL::clear();

    // 2. Clear framebuffer to blank white background
    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // 3. Render "Sleeping zzzz" centered in the middle of the screen
    typography.setFontSize(48.0f);
    std::string sleepMsg = "Sleeping zzzz";
    int tw = typography.measureText(sleepMsg);
    int tx = (w - tw) / 2;
    int ty = (h - 48) / 2;

    typography.renderText(sleepMsg, tx, ty, framebuffer, 0x00);
    DisplayHAL::display(framebuffer);

    DisplayHAL::setPortrait(wasPortrait);
}

void loop() {
    processSerialCommands();

    uint32_t now = millis();
    if (lastTouchActivityTime == 0) {
        lastTouchActivityTime = now;
    }

    int tx, ty;
    bool touched = DisplayHAL::getTouch(tx, ty);

    if (touched) {
        if (isSystemSleeping) {
            // Wake up from sleep on touch!
            isSystemSleeping = false;
            lastTouchActivityTime = now;
            lastTouchTime = 0; // Reset debounce timer to zero for instant waking touch responsiveness

            DisplayHAL::clear();
            if (Launcher::getInstance().getActiveApp()) {
                Launcher::getInstance().getActiveApp()->draw();
            } else {
                Launcher::getInstance().drawMenu();
            }
            return; // Consume touch for waking up
        }
        lastTouchActivityTime = now;
    }

    if (isSystemSleeping) {
#ifndef NATIVE_TESTING
        delay(100);
#else
        DisplayHAL::handleEvents();
        if (DisplayHAL::windowShouldClose()) exit(0);
        usleep(100000);
#endif
        return;
    }

    // Trigger 30-second inactivity sleep
    if ((now - lastTouchActivityTime) >= 30000) {
        isSystemSleeping = true;
        drawSleepScreen();
        return;
    }

    Launcher::getInstance().loop();

    static uint32_t touch_loop_interval = 0;
#ifndef NATIVE_TESTING
    if (now > touch_loop_interval) {
        touch_loop_interval = now + 20; // 50 Hz polling
        if (touched) {
            if (now - lastTouchTime > 800) { // Debounce
                Serial.printf("Handling touch at %d, %d\n", tx, ty);
                Launcher::getInstance().handleTouch(tx, ty);
                lastTouchTime = now;
            }
        }
    }
#else
    if (touched) {
        printf("Handling touch at %d, %d\n", tx, ty);
        Launcher::getInstance().handleTouch(tx, ty);
    }
    
    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) {
        exit(0);
    }
    usleep(100000); // 100ms
#endif
}

#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    setup();
    while (true) {
        loop();
    }
    return 0;
}
#endif

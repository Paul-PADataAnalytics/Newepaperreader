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

#ifndef NATIVE_TESTING
unsigned long lastTouchTime = 0;
#endif

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
    Serial.println("Starting LilyGO EPD47 E-Reader System v1.01a...");
#else
    printf("Starting LilyGO EPD47 E-Reader System v1.01a (Native Mock)...\n");
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

void loop() {
    processSerialCommands();
    Launcher::getInstance().loop();

    static uint32_t touch_loop_interval = 0;
    int tx, ty;
#ifndef NATIVE_TESTING
    if (millis() > touch_loop_interval) {
        touch_loop_interval = millis() + 20; // 50 Hz polling
        if (DisplayHAL::getTouch(tx, ty)) {
            if (millis() - lastTouchTime > 800) { // Debounce
                Serial.printf("Handling touch at %d, %d\n", tx, ty);
                Launcher::getInstance().handleTouch(tx, ty);
                lastTouchTime = millis();
            }
        }
    }
#else
    if (DisplayHAL::getTouch(tx, ty)) {
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

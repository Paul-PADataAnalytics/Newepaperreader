#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#else
#include <stdio.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#endif

#include "DisplayHAL.h"

// Define SPI pins for SD Card (adjust if using ESP32-S3 version)
#define SD_MISO 19
#define SD_MOSI 23
#define SD_SCLK 18
#define SD_CS   5

uint8_t *framebuffer;

void setup() {
#ifndef NATIVE_TESTING
    Serial.begin(115200);
    Serial.println("Starting LilyGO EPD47 E-Reader...");
#else
    printf("Starting LilyGO EPD47 E-Reader (Native Mock)...\n");
#endif

    // Initialize EPD driver via HAL
    DisplayHAL::init();

    // Allocate framebuffer
    framebuffer = DisplayHAL::allocateFramebuffer();
    if (!framebuffer) {
#ifndef NATIVE_TESTING
        Serial.println("Failed to allocate framebuffer!");
#else
        printf("Failed to allocate framebuffer!\n");
#endif
        return;
    }

    // Clear framebuffer (0xFF is white)
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    DisplayHAL::powerOn();
    DisplayHAL::clear();
    
    // Push the white framebuffer to the screen
    DisplayHAL::display(framebuffer);
    DisplayHAL::powerOff();
    
#ifndef NATIVE_TESTING
    Serial.println("Screen initialized and cleared.");
#else
    printf("Screen initialized and cleared.\n");
#endif

#ifndef NATIVE_TESTING
    // Initialize SD Card
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("SD Card Mount Failed!");
    } else {
        Serial.println("SD Card initialized.");
        uint8_t cardType = SD.cardType();
        if(cardType == CARD_NONE){
            Serial.println("No SD card attached");
        } else {
            Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
        }
    }
#endif
}

void loop() {
    // E-readers usually sleep most of the time to save power.
    // For now, just idle.
#ifdef NATIVE_TESTING
    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) {
        // Exit in native mode when window closed
        exit(0);
    }
    usleep(100000); // Sleep 100ms
#else
    delay(1000);
#endif
}

#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    bool isTestMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            isTestMode = true;
            break;
        }
    }

    setup();

    if (isTestMode) {
        printf("Running headless test mode...\n");
        // Inject a simulated touch event
        DisplayHAL::injectTouch(100, 200);

        // Process touch and update UI
        int tx, ty;
        if (DisplayHAL::getTouch(tx, ty)) {
            printf("Touch received at %d, %d. Drawing a rectangle...\n", tx, ty);
            DisplayHAL::drawRect(tx - 10, ty - 10, 20, 20, 0x00, framebuffer); // Draw black rect
            DisplayHAL::display(framebuffer);
        }

        // Dump the framebuffer
        DisplayHAL::dumpFramebuffer("test_output.pgm", framebuffer);
        printf("Test completed. Framebuffer dumped to test_output.pgm\n");
        return 0;
    }

    while (true) {
        loop();
    }
    return 0;
}
#endif

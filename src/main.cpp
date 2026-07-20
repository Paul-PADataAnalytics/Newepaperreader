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
#include "AppComm.h"
#include "ui/UIFramework.h"
#include "TypographyEngine.h"
#include "EpubParser.h"
#include "reader/FileBrowser.h"

// SD Pins are defined in LilyGo-EPD47 utilities.h

uint8_t *framebuffer;

enum AppState {
    STATE_LIBRARY,
    STATE_READING
};

AppState currentState = STATE_LIBRARY;
FileBrowser fileBrowser;
TypographyEngine typography;
EpubParser epubParser;

std::vector<FileInfo> libraryFiles;
std::string currentBookText = "";
int currentReadingOffset = 0; // simplistic pagination

void drawLibrary() {
    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, "Library - /books", 100);
    
    fileBrowser.setRoot("/books");
    libraryFiles = fileBrowser.getFiles();
    
    int y = 60;
    for (size_t i = 0; i < libraryFiles.size(); i++) {
        if (y > 540 - 60) break;
        UIFramework::drawButton(framebuffer, 40, y, 880, 50, libraryFiles[i].name.c_str());
        y += 60;
    }
    DisplayHAL::display(framebuffer);
}

std::string stripHTML(const std::string& html) {
    std::string text;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) {
            text += c;
        }
    }
    return text;
}

void drawReading() {
    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    std::string textToRender = currentBookText.substr(currentReadingOffset);
    typography.renderText(textToRender, 0, 0, framebuffer);
    DisplayHAL::display(framebuffer);
}

void openBook(int index) {
    if (index < 0 || index >= libraryFiles.size()) return;
    
    std::string path = libraryFiles[index].path;
#ifndef NATIVE_TESTING
    path = "/sd" + path;
#endif

#ifndef NATIVE_TESTING
    Serial.printf("Opening book: %s\n", path.c_str());
#endif

    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, "Loading...", 100);
    DisplayHAL::display(framebuffer);

    if (epubParser.open(path)) {
        auto chapters = epubParser.getChapterList();
        if (!chapters.empty()) {
            std::string html = epubParser.getFileContent(chapters[0]);
            currentBookText = stripHTML(html);
        } else {
            currentBookText = "No chapters found in EPUB.";
        }
        epubParser.close();
    } else {
        currentBookText = "Failed to parse EPUB.";
    }

    currentReadingOffset = 0;
    currentState = STATE_READING;
    drawReading();
}

void handleTouch(int x, int y) {
    if (currentState == STATE_LIBRARY) {
        int index = (y - 60) / 60;
        if (index >= 0 && index < libraryFiles.size()) {
            openBook(index);
        }
    } else if (currentState == STATE_READING) {
        if (y < 60 && x < 100) {
            currentState = STATE_LIBRARY;
            drawLibrary();
        } else if (x > 960 / 2) {
            currentReadingOffset += 1200; // rough guess
            if (currentReadingOffset > currentBookText.length()) currentReadingOffset = currentBookText.length();
            drawReading();
        } else {
            currentReadingOffset -= 1200;
            if (currentReadingOffset < 0) currentReadingOffset = 0;
            drawReading();
        }
    }
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
            Serial.println(); // newline to ensure next string works
            Serial.println("SCREENSHOT_END");
        }
    }
#endif
}

void setup() {
#ifndef NATIVE_TESTING
    Serial.begin(115200);
    Serial.println("Starting LilyGO EPD47 E-Reader...");
#else
    printf("Starting LilyGO EPD47 E-Reader (Native Mock)...\n");
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
    
#ifndef NATIVE_TESTING
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

    UIFramework::init();
    
#ifndef NATIVE_TESTING
    if (!typography.loadFont("/sd/Roboto-Regular.ttf", 32)) {
        Serial.println("Failed to load /sd/Roboto-Regular.ttf");
    }
#else
    if (!typography.loadFont("Roboto-Regular.ttf", 32)) {
        printf("Failed to load Roboto-Regular.ttf\n");
    }
#endif

    drawLibrary();
}

void loop() {
    processSerialCommands();

    int tx, ty;
    if (DisplayHAL::getTouch(tx, ty)) {
#ifndef NATIVE_TESTING
        Serial.printf("Handling touch at %d, %d\n", tx, ty);
#else
        printf("Handling touch at %d, %d\n", tx, ty);
#endif
        handleTouch(tx, ty);
    }

#ifdef NATIVE_TESTING
    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) {
        exit(0);
    }
    usleep(100000); // 100ms
#else
    delay(100);
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

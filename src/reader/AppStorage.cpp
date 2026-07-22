#include "AppStorage.h"

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "utilities.h"
#else
#include <cstdio>
#endif

namespace {

std::string stripSdPrefix(const std::string& path) {
    if (path.rfind("/sd", 0) == 0) {
        return path.substr(3);
    }
    return path;
}

} // namespace

namespace AppStorage {

bool initialize() {
#ifdef NATIVE_TESTING
    return true;
#else
    // Recover SD card from potential crash state by sending 80 dummy clock cycles.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(SD_MOSI, OUTPUT);
    digitalWrite(SD_MOSI, HIGH);
    pinMode(SD_SCLK, OUTPUT);
    for (int i = 0; i < 80; i++) {
        digitalWrite(SD_SCLK, HIGH);
        delayMicroseconds(10);
        digitalWrite(SD_SCLK, LOW);
        delayMicroseconds(10);
    }

    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    bool sdMounted = SD.begin(SD_CS, SPI, 4000000, "/sd", 5, false);
    if (!sdMounted) {
        Serial.println("Retrying SD Card Mount at 1MHz...");
        delay(100);
        sdMounted = SD.begin(SD_CS, SPI, 1000000, "/sd", 5, false);
    }

    if (!sdMounted) {
        Serial.println("SD Card Mount Failed! Cannot access SD card.");
        return false;
    }

    Serial.println("SD Card initialized successfully.");
    Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));

    Serial.println("Testing SD root directory...");
    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory '/'");
    } else {
        Serial.println("Root directory opened. Listing files:");
        File file = root.openNextFile();
        while (file) {
            Serial.printf(" - %s (Dir: %d, Size: %d)\n", file.name(), file.isDirectory(), file.size());
            file = root.openNextFile();
        }
        Serial.println("End of root directory listing.");
    }

    return true;
#endif
}

std::string toRuntimePath(const std::string& browserPath) {
#ifdef NATIVE_TESTING
    // On native: FileBrowser gives paths like /books/test.md; strip leading /
    // so fopen() uses a relative path from the working directory (data/)
    if (browserPath.length() > 0 && browserPath[0] == '/') {
        return browserPath.substr(1);
    }
    return browserPath;
#else
    // On device: SD.open() paths are relative to the SD mount root.
    // FileBrowser already produces paths like /books/test.md — use them directly.
    // Strip any accidental /sd prefix if present.
    if (browserPath.rfind("/sd", 0) == 0) {
        return browserPath.substr(3); // strip /sd, keep /books/...
    }
    return browserPath;
#endif
}

std::string toVfsPath(const std::string& runtimePath) {
#ifdef NATIVE_TESTING
    return runtimePath;
#else
    // SD.open() uses relative paths (e.g., /books/test.md)
    // C standard library fopen() needs the absolute VFS mount path.
    if (runtimePath.rfind("/sd", 0) == 0) {
        return runtimePath;
    }
    return "/sd" + runtimePath;
#endif
}

std::string toBookmarkPath(const std::string& runtimeBookPath) {
    // runtimeBookPath is already SD-relative (e.g. /books/test.md)
    // Bookmark lives alongside the book: /books/test.md.bmk
    return runtimeBookPath + ".bmk";
}

bool saveBookmark(const std::string& runtimeBookPath, int offset) {
#ifdef NATIVE_TESTING
    std::string path = toBookmarkPath(runtimeBookPath);
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        return false;
    }
    fprintf(f, "%d", offset);
    fclose(f);
    return true;
#else
    std::string path = toBookmarkPath(runtimeBookPath);
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        return false;
    }
    f.printf("%d", offset);
    f.close();
    return true;
#endif
}

int loadBookmark(const std::string& runtimeBookPath) {
#ifdef NATIVE_TESTING
    std::string path = toBookmarkPath(runtimeBookPath);
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        return 0;
    }
    int offset = 0;
    fscanf(f, "%d", &offset);
    fclose(f);
    return offset;
#else
    std::string path = toBookmarkPath(runtimeBookPath);
    // Only attempt to open if the file exists — avoids VFS error spam
    if (!SD.exists(path.c_str())) return 0;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
        return 0;
    }
    String s = f.readString();
    f.close();
    return s.toInt();
#endif
}

} // namespace AppStorage

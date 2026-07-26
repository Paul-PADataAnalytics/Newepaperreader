#include "AppStorage.h"
#include <cstdint>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "utilities.h"
#else
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <unistd.h>
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

bool saveBookmark(const std::string& runtimeBookPath, int offset, int chapterIndex, int totalChapters) {
#ifdef NATIVE_TESTING
    std::string path = toBookmarkPath(runtimeBookPath);
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        return false;
    }
    fprintf(f, "%d\n%d\n%d", offset, chapterIndex, totalChapters);
    fclose(f);
    return true;
#else
    std::string path = toBookmarkPath(runtimeBookPath);
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        return false;
    }
    f.printf("%d\n%d\n%d", offset, chapterIndex, totalChapters);
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

BookmarkInfo loadBookmarkInfo(const std::string& runtimeBookPath) {
    BookmarkInfo info;
#ifdef NATIVE_TESTING
    std::string path = toBookmarkPath(runtimeBookPath);
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return info;
    fscanf(f, "%d", &info.offset);
    if (fscanf(f, "%d", &info.chapterIndex) != 1) info.chapterIndex = 0;
    if (fscanf(f, "%d", &info.totalChapters) != 1) info.totalChapters = 0;
    fclose(f);
    return info;
#else
    std::string path = toBookmarkPath(runtimeBookPath);
    if (!SD.exists(path.c_str())) return info;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return info;
    String s = f.readString();
    f.close();

    int firstNl = s.indexOf('\n');
    if (firstNl < 0) {
        info.offset = s.toInt();
        return info;
    }
    info.offset = s.substring(0, firstNl).toInt();

    int secondNl = s.indexOf('\n', firstNl + 1);
    if (secondNl < 0) {
        info.chapterIndex = s.substring(firstNl + 1).toInt();
        return info;
    }
    info.chapterIndex = s.substring(firstNl + 1, secondNl).toInt();
    info.totalChapters = s.substring(secondNl + 1).toInt();
    return info;
#endif
}

bool copyFile(const std::string& sourcePath, const std::string& destinationPath,
              std::string& error) {
    if (sourcePath == destinationPath) {
        error = "Source and destination are the same.";
        return false;
    }

#ifdef NATIVE_TESTING
    if (access(destinationPath.c_str(), F_OK) == 0) {
        error = "A file with that name already exists.";
        return false;
    }

    FILE* source = fopen(sourcePath.c_str(), "rb");
    if (!source) {
        error = std::string("Cannot open source: ") + strerror(errno);
        return false;
    }
    FILE* destination = fopen(destinationPath.c_str(), "wb");
    if (!destination) {
        error = std::string("Cannot create destination: ") + strerror(errno);
        fclose(source);
        return false;
    }

    uint8_t buffer[4096];
    bool ok = true;
    size_t bytesRead = 0;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, bytesRead, destination) != bytesRead) {
            error = "Failed while writing destination.";
            ok = false;
            break;
        }
    }
    if (ferror(source)) {
        error = "Failed while reading source.";
        ok = false;
    }
    if (fclose(destination) != 0 && ok) {
        error = "Failed to finish writing destination.";
        ok = false;
    }
    fclose(source);
    if (!ok) {
        remove(destinationPath.c_str());
    }
    return ok;
#else
    if (SD.exists(destinationPath.c_str())) {
        error = "A file with that name already exists.";
        return false;
    }

    File source = SD.open(sourcePath.c_str(), FILE_READ);
    if (!source || source.isDirectory()) {
        error = "Cannot open source file.";
        if (source) source.close();
        return false;
    }
    File destination = SD.open(destinationPath.c_str(), FILE_WRITE);
    if (!destination) {
        error = "Cannot create destination file.";
        source.close();
        return false;
    }

    uint8_t buffer[4096];
    bool ok = true;
    size_t expectedBytes = source.size();
    size_t copiedBytes = 0;
    while (source.available()) {
        size_t bytesRead = source.read(buffer, sizeof(buffer));
        if (bytesRead == 0 || destination.write(buffer, bytesRead) != bytesRead) {
            error = "Failed while copying file.";
            ok = false;
            break;
        }
        copiedBytes += bytesRead;
    }
    if (ok && copiedBytes != expectedBytes) {
        error = "Source file could not be read completely.";
        ok = false;
    }
    destination.close();
    source.close();
    if (!ok) {
        SD.remove(destinationPath.c_str());
    }
    return ok;
#endif
}

bool moveFile(const std::string& sourcePath, const std::string& destinationPath,
              std::string& error) {
    if (sourcePath == destinationPath) {
        error = "Source and destination are the same.";
        return false;
    }

#ifdef NATIVE_TESTING
    if (access(destinationPath.c_str(), F_OK) == 0) {
        error = "A file with that name already exists.";
        return false;
    }
    if (rename(sourcePath.c_str(), destinationPath.c_str()) != 0) {
        error = std::string("Move failed: ") + strerror(errno);
        return false;
    }
#else
    if (SD.exists(destinationPath.c_str())) {
        error = "A file with that name already exists.";
        return false;
    }
    if (!SD.rename(sourcePath.c_str(), destinationPath.c_str())) {
        error = "Move failed.";
        return false;
    }
#endif
    return true;
}

bool deleteFile(const std::string& path, std::string& error) {
#ifdef NATIVE_TESTING
    if (remove(path.c_str()) != 0) {
        error = std::string("Delete failed: ") + strerror(errno);
        return false;
    }
#else
    if (!SD.remove(path.c_str())) {
        error = "Delete failed.";
        return false;
    }
#endif
    return true;
}

bool saveSystemState(const SavedSystemState& state) {
    std::string path = toRuntimePath("/data/system_state.txt");
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "%d\n%d\n%d\n%s\n", state.appIndex, state.internalState, state.isPortrait ? 1 : 0, state.path.c_str());
    fclose(f);
    return true;
#else
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.printf("%d\n%d\n%d\n%s\n", state.appIndex, state.internalState, state.isPortrait ? 1 : 0, state.path.c_str());
    f.close();
    return true;
#endif
}

SavedSystemState loadSystemState() {
    SavedSystemState state;
    std::string path = toRuntimePath("/data/system_state.txt");
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return state;
    int appIdx = -1, intState = 0, portrait = 0;
    char pathBuf[512] = {0};
    if (fscanf(f, "%d\n%d\n%d\n", &appIdx, &intState, &portrait) == 3) {
        if (fgets(pathBuf, sizeof(pathBuf), f)) {
            size_t len = strlen(pathBuf);
            while (len > 0 && (pathBuf[len - 1] == '\r' || pathBuf[len - 1] == '\n')) {
                pathBuf[--len] = '\0';
            }
            state.path = pathBuf;
        }
        state.appIndex = appIdx;
        state.internalState = intState;
        state.isPortrait = (portrait != 0);
        state.valid = (appIdx >= 0);
    }
    fclose(f);
    return state;
#else
    if (!SD.exists(path.c_str())) return state;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return state;
    String l1 = f.readStringUntil('\n'); l1.trim();
    String l2 = f.readStringUntil('\n'); l2.trim();
    String l3 = f.readStringUntil('\n'); l3.trim();
    String l4 = f.readStringUntil('\n'); l4.trim();
    f.close();

    state.appIndex = l1.toInt();
    state.internalState = l2.toInt();
    state.isPortrait = (l3.toInt() != 0);
    state.path = l4.c_str();
    state.valid = (state.appIndex >= 0);
    return state;
#endif
}

bool clearSystemState() {
    std::string path = toRuntimePath("/data/system_state.txt");
#ifdef NATIVE_TESTING
    remove(path.c_str());
    return true;
#else
    if (SD.exists(path.c_str())) {
        SD.remove(path.c_str());
    }
    return true;
#endif
}

} // namespace AppStorage


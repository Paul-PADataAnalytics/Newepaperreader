#include "AppStorage.h"
#include <cstdint>
#include <cstdlib>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
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

// Turns a scan root path (e.g. "/books") into a filesystem-safe cache
// filename fragment (e.g. "_books"), so different scanned roots each get
// their own persisted index file.
std::string sanitizeRootForFilename(const std::string& rootPath) {
    std::string out = rootPath;
    for (char& c : out) {
        if (c == '/' || c == '\\' || c == ' ') c = '_';
    }
    return out;
}

const char* kLibraryDirtyFlagPath = "/data/library_dirty.flag";

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
    } else {
        markLibraryDirty();
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
    } else {
        markLibraryDirty();
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
    markLibraryDirty();
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
    markLibraryDirty();
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

bool saveLastBook(const LastBook& book) {
    std::string path = toRuntimePath("/data/last_book.txt");
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "%s\n%s\n%s\n%d\n", book.path.c_str(), book.title.c_str(), book.author.c_str(), book.percent);
    fclose(f);
#else
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.printf("%s\n%s\n%s\n%d\n", book.path.c_str(), book.title.c_str(), book.author.c_str(), book.percent);
    f.close();
#endif
    return true;
}

LastBook loadLastBook() {
    LastBook book;
    std::string path = toRuntimePath("/data/last_book.txt");
    std::vector<std::string> lines;
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return book;
    char buf[512];
    while (lines.size() < 4 && fgets(buf, sizeof(buf), f)) {
        std::string l = buf;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        lines.push_back(l);
    }
    fclose(f);
#else
    if (!SD.exists(path.c_str())) return book;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return book;
    while (lines.size() < 4 && f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        lines.push_back(l.c_str());
    }
    f.close();
#endif
    if (lines.size() < 4 || lines[0].empty()) return book;
    book.path = lines[0];
    book.title = lines[1];
    book.author = lines[2];
    book.percent = atoi(lines[3].c_str());
    book.valid = true;
    return book;
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

bool wasResumedFromSleep() {
#ifdef NATIVE_TESTING
    return false;
#else
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#endif
}

void markLibraryDirty() {
    std::string path = toRuntimePath(kLibraryDirtyFlagPath);
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "w");
    if (f) fclose(f);
#else
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (f) f.close();
#endif
}

bool saveLibraryIndex(const std::string& rootPath, const std::vector<LibraryIndexEntry>& entries) {
    std::string path = toRuntimePath("/data/lib_index" + sanitizeRootForFilename(rootPath) + ".txt");
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (const auto& e : entries) {
        fprintf(f, "%u\t%s\n", (unsigned)e.size, e.path.c_str());
    }
    fclose(f);
#else
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    for (const auto& e : entries) {
        f.printf("%u\t%s\n", (unsigned)e.size, e.path.c_str());
    }
    f.close();
#endif
    // A fresh, successfully-saved scan is authoritative again.
    std::string dirtyPath = toRuntimePath(kLibraryDirtyFlagPath);
#ifdef NATIVE_TESTING
    remove(dirtyPath.c_str());
#else
    if (SD.exists(dirtyPath.c_str())) SD.remove(dirtyPath.c_str());
#endif
    return true;
}

LibraryIndex loadLibraryIndex(const std::string& rootPath) {
    LibraryIndex result;

    // A fresh power-on means the SD card could have been swapped or edited
    // externally - never trust a cached scan in that case, only across a
    // sleep-resume (where the card was never unmounted).
    if (!wasResumedFromSleep()) return result;

    std::string dirtyPath = toRuntimePath(kLibraryDirtyFlagPath);
#ifdef NATIVE_TESTING
    if (access(dirtyPath.c_str(), F_OK) == 0) return result;
#else
    if (SD.exists(dirtyPath.c_str())) return result;
#endif

    std::string path = toRuntimePath("/data/lib_index" + sanitizeRootForFilename(rootPath) + ".txt");
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return result;
    char lineBuf[600];
    while (fgets(lineBuf, sizeof(lineBuf), f)) {
        unsigned size = 0;
        char pathBuf[512] = {0};
        if (sscanf(lineBuf, "%u\t%511[^\n]", &size, pathBuf) == 2) {
            LibraryIndexEntry entry;
            entry.size = size;
            entry.path = pathBuf;
            result.entries.push_back(entry);
        }
    }
    fclose(f);
    result.valid = true;
#else
    if (!SD.exists(path.c_str())) return result;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return result;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int tabPos = line.indexOf('\t');
        if (tabPos < 0) continue;
        LibraryIndexEntry entry;
        entry.size = (uint32_t)line.substring(0, tabPos).toInt();
        entry.path = line.substring(tabPos + 1).c_str();
        result.entries.push_back(entry);
    }
    f.close();
    result.valid = true;
#endif
    return result;
}

} // namespace AppStorage


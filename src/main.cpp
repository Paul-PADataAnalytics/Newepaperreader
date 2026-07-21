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
#endif

#include "DisplayHAL.h"
#include "AppComm.h"
#include "ui/UIFramework.h"
#include "TypographyEngine.h"
#include "EpubParser.h"
#include "embedded_font.h"
#include "reader/AppStorage.h"
#include "reader/FileBrowser.h"
#include "reader/TextReader.h"
#include "comm/WiFiSync.h"
#include "ui/AppScreens.h"

// SD Pins are defined in LilyGo-EPD47 utilities.h

uint8_t *framebuffer;

enum AppState {
    STATE_LIBRARY,
    STATE_READING,
    STATE_WIFI_SYNC
};

AppState currentState = STATE_LIBRARY;
FileBrowser fileBrowser;
TypographyEngine typography;
EpubParser epubParser;
TextReader textReader;

#ifndef NATIVE_TESTING
unsigned long lastTouchTime = 0;
#endif

std::vector<FileInfo> libraryFiles;
char* currentBookText = nullptr;
size_t currentBookTextLen = 0;
int currentReadingOffset = 0; // simplistic pagination
std::string currentBookPath = ""; // to save bookmarks

// Library UI Constants
const int LIB_TOP_H = 54;
const int LIB_SIDE_W = 144;
const int LIB_SIDE_X = 960 - LIB_SIDE_W; // 816
const int LIB_MAIN_W = 816;
const int BOTTOM_MARGIN = 14; // ~2.5% of 540
const int LIB_MAIN_H = 540 - LIB_TOP_H - BOTTOM_MARGIN; // 472
const int LIB_MAIN_Y = 54;
const int LIB_PAGING_W = 82;
const int LIB_LIST_X = LIB_PAGING_W;
const int LIB_LIST_W = LIB_MAIN_W - LIB_PAGING_W; // 734
const int LIB_ROW_H = 68; // Height of each row (allowing 20% padding)

int libraryPage = 0;
int librarySidebarPage = 0;

enum class LibrarySort { AUTHOR, GENRE, COMPLETION };
LibrarySort currentLibrarySort = LibrarySort::COMPLETION;

struct LibraryItem {
    FileInfo file;
    int completionPercent;
    std::string author;
    std::string title;
};
std::vector<LibraryItem> parsedLibraryItems;

void updateLibraryItems();


void updateLibraryItems() {
    parsedLibraryItems.clear();
    fileBrowser.setRoot("/books");
    std::vector<FileInfo> allFiles = fileBrowser.getFiles();
    libraryFiles.clear();

    for (auto& f : allFiles) {
        if (f.isDirectory) continue;
        
        // Only allow .epub and .txt files
        std::string lowerName = f.name;
        for (char& c : lowerName) c = tolower(c);
        if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".epub") {
            // Valid
        } else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".txt") {
            // Valid
        } else {
            continue;
        }

        libraryFiles.push_back(f);

        LibraryItem item;
        item.file = f;
        item.title = f.name;
        item.author = "Unknown Author";
        
        size_t dotPos = item.title.find_last_of('.');
        if (dotPos != std::string::npos) {
            item.title = item.title.substr(0, dotPos);
        }
        
    int offset = AppStorage::loadBookmark(AppStorage::toRuntimePath("/books/" + f.name));

        if (offset > 0 && f.size > 0) {
            // Rough estimation
            item.completionPercent = (offset * 100) / (f.size * 2); 
            if (item.completionPercent > 100) item.completionPercent = 100;
            if (item.completionPercent == 0) item.completionPercent = 1;
        } else {
            item.completionPercent = 0;
        }
        
        if (item.completionPercent >= 97) {
            item.completionPercent = -100;
        }
        
        parsedLibraryItems.push_back(item);
    }
    
    // Simple sort
    for (size_t i = 0; i < parsedLibraryItems.size(); i++) {
        for (size_t j = i + 1; j < parsedLibraryItems.size(); j++) {
            bool swap = false;
            if (currentLibrarySort == LibrarySort::COMPLETION) {
                swap = parsedLibraryItems[j].completionPercent > parsedLibraryItems[i].completionPercent;
            } else if (currentLibrarySort == LibrarySort::AUTHOR) {
                swap = parsedLibraryItems[j].author < parsedLibraryItems[i].author;
            } else {
                swap = parsedLibraryItems[j].title < parsedLibraryItems[i].title;
            }
            if (swap) {
                LibraryItem temp = parsedLibraryItems[i];
                parsedLibraryItems[i] = parsedLibraryItems[j];
                parsedLibraryItems[j] = temp;
            }
        }
    }
}

void drawLibrary() {
    // 1. Top Bar (10% Height)
    UIFramework::clearArea(framebuffer, 0, 0, 960, LIB_TOP_H);
    typography.renderText("Library", 20, 0, framebuffer);
    
    // Mock system info
    std::string sysInfo = "10:00 AM | 80% | 12GB Free";
    int sysInfoW = typography.measureText(sysInfo);
    typography.renderText(sysInfo, 960 - sysInfoW, 0, framebuffer);
    DisplayHAL::drawHLine(0, LIB_TOP_H - 1, 960, 0x00, framebuffer);

    // 2. Right Sidebar (15% Width)
    UIFramework::clearArea(framebuffer, LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H);
    DisplayHAL::drawRect(LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H, 0x00, framebuffer);
    
    // Draw up to 4 buttons
    std::vector<std::pair<std::string, std::string>> buttons = {
        {"Sort:", "Author"},
        {"Sort:", "Genre"},
        {"Sort:", "Prog %"}
    };
    int btnH = LIB_MAIN_H / 4;
    typography.setFontSize(24.0f); // Make text smaller for sidebar buttons
    for (size_t i = 0; i < buttons.size() && i < 4; i++) {
        int by = LIB_MAIN_Y + (i * btnH);
        UIFramework::drawButton(framebuffer, LIB_SIDE_X + 10, by + 10, LIB_SIDE_W - 20, btnH - 20, "");
        
        // Fill from top, centered horizontally
        int tw1 = typography.measureText(buttons[i].first);
        int tw2 = typography.measureText(buttons[i].second);
        int tx1 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw1) / 2;
        int tx2 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw2) / 2;
        int ty = by + 25; // 15px from button top boundary
        typography.renderText(buttons[i].first, tx1, ty, framebuffer);
        typography.renderText(buttons[i].second, tx2, ty + 30, framebuffer);
    }
    typography.setFontSize(32.0f); // Restore default

    // 3. Main List Area (85% Width, 90% Height)
    // Lightest greyscale background: 0xDD (or alternating pixels if 4-bit)
    // EPD47 framebuffer uses 4-bit grayscale, 0xFF is white, 0x00 is black.
    // Let's use 0xDD for very light grey (0xEE is often invisible).
    UIFramework::clearArea(framebuffer, 0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H);
    DisplayHAL::fillRect(0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H, 0xDD, framebuffer);

    // List Paging Area (Left 10% of main area)
    int pBtnW = LIB_PAGING_W - 20;
    int pBtnH = LIB_MAIN_H / 2 - 20;
    int symbolHeight = 24;
    
    int upY = LIB_MAIN_Y + 10;
    UIFramework::drawButton(framebuffer, 25, upY, pBtnW, pBtnH, "");
    std::string upSymbol = "/\\";
    int twUp = typography.measureText(upSymbol);
    typography.renderText(upSymbol, 25 + (pBtnW - twUp) / 2, upY + (pBtnH - symbolHeight) / 2, framebuffer);
    
    int dnY = LIB_MAIN_Y + LIB_MAIN_H / 2 + 10;
    UIFramework::drawButton(framebuffer, 25, dnY, pBtnW, pBtnH, "");
    std::string dnSymbol = "\\/";
    int twDn = typography.measureText(dnSymbol);
    typography.renderText(dnSymbol, 25 + (pBtnW - twDn) / 2, dnY + (pBtnH - symbolHeight) / 2, framebuffer);

    // Render List Items
    updateLibraryItems();
    
    int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
    int startIndex = libraryPage * itemsPerPage;
    int y = LIB_MAIN_Y;
    typography.setFontSize(40.0f);
    
    for (int i = 0; i < itemsPerPage && (startIndex + i) < parsedLibraryItems.size(); i++) {
        LibraryItem& item = parsedLibraryItems[startIndex + i];
        
        // Push down a bit so larger text is vertically centered
        int rowY = y + 20;  
        
        std::string leftText = item.title + " - " + item.author;
        
        // Clamp leftText
        int maxW = LIB_LIST_W - 100; // Leave 100px for percentage
        while (leftText.length() > 3 && typography.measureText(leftText + "...") > maxW) {
            leftText.pop_back();
        }
        if (leftText.length() < item.title.length() + item.author.length() + 3) {
            leftText += "...";
        }
        
        std::string rightText = item.completionPercent == -100 ? "100%" : std::to_string(item.completionPercent) + "%";
        int rw = typography.measureText(rightText);
        typography.renderText(leftText, LIB_LIST_X + 20, rowY, framebuffer);
        typography.renderText(rightText, LIB_LIST_X + LIB_LIST_W - rw - 20, rowY, framebuffer);
        
        y += LIB_ROW_H;
    }
    
    if (parsedLibraryItems.empty()) {
        typography.renderText("No books found.", LIB_LIST_X + 20, LIB_MAIN_Y + 50, framebuffer);
    }
    typography.setFontSize(32.0f); // Restore after rendering the list
    
    DisplayHAL::display(framebuffer);
}

static void appendNormalizedChar(std::string& out, char c) {
    if (c == '\r' || c == '\t') {
        c = ' ';
    }
    if (c == '\n') {
        if (!out.empty() && out.back() != '\n') {
            out.push_back('\n');
        }
        return;
    }
    if (isspace(static_cast<unsigned char>(c))) {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
            out.push_back(' ');
        }
        return;
    }
    out.push_back(c);
}

static void appendBlockBreak(std::string& out) {
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    if (out.empty() || out.back() != '\n') {
        out.push_back('\n');
    }
}

static char decodeHtmlEntity(const std::string& entity) {
    if (entity == "nbsp") return ' ';
    if (entity == "amp") return '&';
    if (entity == "lt") return '<';
    if (entity == "gt") return '>';
    if (entity == "quot") return '"';
    if (entity.size() > 1 && entity[0] == '#') {
        int base = 10;
        size_t start = 1;
        if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
            base = 16;
            start = 2;
        }
        char* end = nullptr;
        long value = strtol(entity.c_str() + start, &end, base);
        if (end && *end == '\0') {
            if (value == 160) return ' ';
            if (value >= 32 && value <= 126) return static_cast<char>(value);
        }
    }
    return ' ';
}

char* stripHTML(const char* html, size_t len, size_t& outLen) {
    if (!html) {
        outLen = 0;
        return nullptr;
    }

    std::string text;
    text.reserve(len);

    for (size_t i = 0; i < len; i++) {
        char c = html[i];
        if (c == '<') {
            size_t tagEnd = i + 1;
            while (tagEnd < len && html[tagEnd] != '>') {
                tagEnd++;
            }
            std::string tag(html + i + 1, html + tagEnd);
            std::string lowerTag;
            lowerTag.reserve(tag.size());
            for (char ch : tag) {
                lowerTag.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
            }
            if (lowerTag.rfind("br", 0) == 0 ||
                lowerTag.rfind("/p", 0) == 0 ||
                lowerTag.rfind("/div", 0) == 0 ||
                lowerTag.rfind("/h", 0) == 0 ||
                lowerTag.rfind("li", 0) == 0 ||
                lowerTag.rfind("/li", 0) == 0 ||
                lowerTag.rfind("/tr", 0) == 0) {
                appendBlockBreak(text);
            }
            i = tagEnd;
            continue;
        }

        if (c == '&') {
            size_t entityEnd = i + 1;
            while (entityEnd < len && html[entityEnd] != ';' && entityEnd - i <= 10) {
                entityEnd++;
            }
            if (entityEnd < len && html[entityEnd] == ';') {
                std::string entity(html + i + 1, html + entityEnd);
                appendNormalizedChar(text, decodeHtmlEntity(entity));
                i = entityEnd;
                continue;
            }
        }

        appendNormalizedChar(text, c);
    }

    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.pop_back();
    }

    outLen = text.size();
#ifndef NATIVE_TESTING
    char* out = (char*)ps_malloc(outLen + 1);
    if (!out) out = (char*)malloc(outLen + 1);
#else
    char* out = (char*)malloc(outLen + 1);
#endif
    if (!out) {
        outLen = 0;
        return nullptr;
    }
    memcpy(out, text.c_str(), outLen);
    out[outLen] = '\0';
    return out;
}

size_t countReadableChars(const char* text, size_t len) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(static_cast<unsigned char>(text[i]))) {
            count++;
        }
    }
    return count;
}

bool isPreferredEpubChapter(const std::string& chapterPath) {
    std::string lower = chapterPath;
    for (char& c : lower) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    if (lower.find("cover") != std::string::npos ||
        lower.find("title") != std::string::npos ||
        lower.find("toc") != std::string::npos ||
        lower.find("contents") != std::string::npos ||
        lower.find("copyright") != std::string::npos ||
        lower.find("newsletter") != std::string::npos ||
        lower.find("abouttheauthor") != std::string::npos ||
        lower.find("acknowledg") != std::string::npos ||
        lower.find("adcard") != std::string::npos ||
        lower.find("torad") != std::string::npos ||
        lower.find("mini_toc") != std::string::npos ||
        lower.find("nav") != std::string::npos) {
        return false;
    }

    return lower.find("chapter") != std::string::npos ||
           lower.find("prologue") != std::string::npos ||
           lower.find("part") != std::string::npos;
}

void drawReading() {
    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    if (currentBookText) {
        size_t remainingLen = currentBookTextLen - currentReadingOffset;
        typography.renderText(currentBookText + currentReadingOffset, remainingLen, 20, 20, framebuffer);
    }
    DisplayHAL::display(framebuffer);
}

void openBook(int index) {
    if (index < 0 || index >= libraryFiles.size()) return;
    
    std::string path = AppStorage::toRuntimePath(libraryFiles[index].path);

#ifndef NATIVE_TESTING
    Serial.printf("Opening book: %s\n", path.c_str());
#endif

    AppScreens::drawLoading(framebuffer, "Loading...");

    bool isText = false;
    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".txt" || path.substr(path.length() - 4) == ".rtf" || path.substr(path.length() - 4) == ".RTF")) isText = true;
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".md") isText = true;

    if (currentBookText) {
        free(currentBookText);
        currentBookText = nullptr;
        currentBookTextLen = 0;
    }

    if (isText) {
        if (textReader.openFile(path.c_str())) {
            std::string text = textReader.getPageText();
            currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
            currentBookText = (char*)ps_malloc(currentBookTextLen + 1);
            if (!currentBookText) currentBookText = (char*)malloc(currentBookTextLen + 1);
#else
            currentBookText = (char*)malloc(currentBookTextLen + 1);
#endif
            if (currentBookText) {
                memcpy(currentBookText, text.c_str(), currentBookTextLen + 1);
            }
        }
    } else if (epubParser.open(path)) {
        auto chapters = epubParser.getChapterList();
        char* fallbackText = nullptr;
        size_t fallbackLen = 0;
        for (const auto& chapter : chapters) {
            size_t size = 0;
            char* html = epubParser.getFileContent(chapter, size);
            if (!html) {
                continue;
            }

            size_t strippedLen = 0;
            char* stripped = stripHTML(html, size, strippedLen);
            free(html);
            if (!stripped) {
                continue;
            }

            size_t readableChars = countReadableChars(stripped, strippedLen);
            if (isPreferredEpubChapter(chapter) && readableChars >= 40) {
                currentBookText = stripped;
                currentBookTextLen = strippedLen;
                break;
            }

            if (!fallbackText && readableChars > 0) {
                fallbackText = stripped;
                fallbackLen = strippedLen;
            } else {
                free(stripped);
            }
        }

        if (!currentBookText && fallbackText) {
            currentBookText = fallbackText;
            currentBookTextLen = fallbackLen;
        }
        epubParser.close();
    }

    currentBookPath = path;
    int savedOffset = AppStorage::loadBookmark(path);

    if (isText && textReader.isOpen()) {
        textReader.setPosition(savedOffset);
        std::string text = textReader.getPageText();
        if (currentBookText) free(currentBookText);
        currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
        currentBookText = (char*)ps_malloc(currentBookTextLen + 1);
        if (!currentBookText) currentBookText = (char*)malloc(currentBookTextLen + 1);
#else
        currentBookText = (char*)malloc(currentBookTextLen + 1);
#endif
        if (currentBookText) {
            memcpy(currentBookText, text.c_str(), currentBookTextLen + 1);
        }
        currentReadingOffset = 0;
    } else {
        currentReadingOffset = savedOffset;
        if (currentReadingOffset < 0) currentReadingOffset = 0;
        if (currentReadingOffset > currentBookTextLen) currentReadingOffset = 0;
    }

    currentState = STATE_READING;
    drawReading();
}

void drawWiFiSync() {
    DisplayHAL::clear(); // Force hardware full refresh
    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, "WiFi Sync Mode", 100);
    
    std::string info = "Connect to WiFi SoftAP: 'EPD-Reader'\n";
    info += "Then open http://192.168.4.1 in your browser.\n\n";
    info += "Tap anywhere to exit WiFi Sync Mode.";
    
    typography.renderText(info, 40, 100, framebuffer);
    DisplayHAL::display(framebuffer);
}

void handleTouch(int x, int y) {
    if (currentState == STATE_LIBRARY) {
        if (x >= LIB_SIDE_X && y >= LIB_MAIN_Y) {
            // Sidebar tapped
            int btnIndex = (y - LIB_MAIN_Y) / (LIB_MAIN_H / 4);
            if (btnIndex == 0) currentLibrarySort = LibrarySort::AUTHOR;
            else if (btnIndex == 1) currentLibrarySort = LibrarySort::GENRE;
            else if (btnIndex == 2) currentLibrarySort = LibrarySort::COMPLETION;
            else if (btnIndex == 3) {
                // 4th button: WiFi Sync
                currentState = STATE_WIFI_SYNC;
                startWiFiSync();
                drawWiFiSync();
                return;
            }
            libraryPage = 0; // Reset page on sort
            drawLibrary();
        } else if (x <= LIB_PAGING_W && y >= LIB_MAIN_Y) {
            // Paging Area tapped
            if (y < LIB_MAIN_Y + LIB_MAIN_H / 2) {
                // UP
                if (libraryPage > 0) libraryPage--;
            } else {
                // DOWN
                int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
                int maxPage = parsedLibraryItems.size() / itemsPerPage;
                if (libraryPage < maxPage) libraryPage++;
            }
            drawLibrary();
        } else if (x > LIB_PAGING_W && x < LIB_SIDE_X && y >= LIB_MAIN_Y) {
            // List item tapped
            int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
            int itemIndex = (y - LIB_MAIN_Y) / LIB_ROW_H;
            int globalIndex = libraryPage * itemsPerPage + itemIndex;
            
            if (globalIndex >= 0 && globalIndex < parsedLibraryItems.size()) {
                std::string targetPath = parsedLibraryItems[globalIndex].file.path;
                for (size_t i = 0; i < libraryFiles.size(); i++) {
                    if (libraryFiles[i].path == targetPath) {
                        openBook(i);
                        break;
                    }
                }
            }
        }
    } else if (currentState == STATE_READING) {
        if (y < 60 && x < 100) {
            if (textReader.isOpen()) {
                AppStorage::saveBookmark(currentBookPath, textReader.getPosition());
                textReader.closeFile();
            } else {
                AppStorage::saveBookmark(currentBookPath, currentReadingOffset);
            }
            currentState = STATE_LIBRARY;
            drawLibrary();
        } else if (x > 960 / 2) {
            if (textReader.isOpen()) {
                textReader.nextPage();
                std::string text = textReader.getPageText();
                if (currentBookText) free(currentBookText);
                currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                currentBookText = (char*)ps_malloc(currentBookTextLen + 1);
                if (!currentBookText) currentBookText = (char*)malloc(currentBookTextLen + 1);
#else
                currentBookText = (char*)malloc(currentBookTextLen + 1);
#endif
                if (currentBookText) memcpy(currentBookText, text.c_str(), currentBookTextLen + 1);
                AppStorage::saveBookmark(currentBookPath, textReader.getPosition());
                currentReadingOffset = 0;
            } else {
                currentReadingOffset += 1200; // rough guess
                if (currentReadingOffset > currentBookTextLen) currentReadingOffset = currentBookTextLen;
                AppStorage::saveBookmark(currentBookPath, currentReadingOffset);
            }
            drawReading();
        } else {
            if (textReader.isOpen()) {
                textReader.prevPage();
                std::string text = textReader.getPageText();
                if (currentBookText) free(currentBookText);
                currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                currentBookText = (char*)ps_malloc(currentBookTextLen + 1);
                if (!currentBookText) currentBookText = (char*)malloc(currentBookTextLen + 1);
#else
                currentBookText = (char*)malloc(currentBookTextLen + 1);
#endif
                if (currentBookText) memcpy(currentBookText, text.c_str(), currentBookTextLen + 1);
                AppStorage::saveBookmark(currentBookPath, textReader.getPosition());
                currentReadingOffset = 0;
            } else {
                currentReadingOffset -= 1200;
                if (currentReadingOffset < 0) currentReadingOffset = 0;
                AppStorage::saveBookmark(currentBookPath, currentReadingOffset);
            }
            drawReading();
        }
    } else if (currentState == STATE_WIFI_SYNC) {
        stopWiFiSync();
        currentState = STATE_LIBRARY;
        drawLibrary();
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
    
    AppStorage::initialize();

    UIFramework::init();
    
#ifndef NATIVE_TESTING
    if (!typography.loadFont("/sd/data/Roboto-Regular.ttf", 48.0f)) {
        Serial.println("Falling back to embedded font...");
        typography.loadFontFromMemory(data_Roboto_Regular_ttf, data_Roboto_Regular_ttf_len, 48.0f);
    }
#else
    if (!typography.loadFont("data/Roboto-Regular.ttf", 32)) {
        printf("Failed to load Roboto-Regular.ttf\n");
    }
#endif

    drawLibrary();
}

void loop() {
    processSerialCommands();

    static uint32_t touch_loop_interval = 0;

    int tx, ty;
#ifndef NATIVE_TESTING
    if (millis() > touch_loop_interval) {
        touch_loop_interval = millis() + 300;
        if (DisplayHAL::getTouch(tx, ty)) {
            lastTouchTime = millis();
            Serial.printf("Handling touch at %d, %d\n", tx, ty);
            handleTouch(tx, ty);
        }
    }
#else
    if (DisplayHAL::getTouch(tx, ty)) {
        printf("Handling touch at %d, %d\n", tx, ty);
        handleTouch(tx, ty);
    }
    
    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) {
        exit(0);
    }
    usleep(100000); // 100ms
#endif

#ifndef NATIVE_TESTING
    if (millis() - lastTouchTime > 15000) {
        // Serial.println("Entering light sleep to save power...");
        // esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_INT, 0); // Wake on touch (LOW)
        // esp_light_sleep_start();
        // Serial.println("Woke up from light sleep!");
        lastTouchTime = millis(); // Reset timer so it doesn't spam
    }
    delay(10);
#endif
}

#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    setup();
    if (argc > 1 && strcmp(argv[1], "--headless") == 0) {
        printf("Running headless test for markdown...\n");
        int indexToOpen = 0;
        int rtfIndex = -1;
        printf("Library files:\n");
        for (int i=0; i<libraryFiles.size(); i++) {
            printf(" - %s\n", libraryFiles[i].name.c_str());
            if (libraryFiles[i].name == "test_document.rtf") rtfIndex = i;
            if (libraryFiles[i].name == "test.md") {
                indexToOpen = i;
            }
        }
        openBook(indexToOpen);
        DisplayHAL::dumpFramebuffer("screenshot_markdown.pgm", framebuffer);
        if (rtfIndex != -1) {
            openBook(rtfIndex);
            DisplayHAL::dumpFramebuffer("screenshot_rtf_test.pgm", framebuffer);
        }
        return 0;
    }
    while (true) {
        loop();
    }
    return 0;
}
#endif

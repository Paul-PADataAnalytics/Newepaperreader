#include "EReaderApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include "embedded_font.h"
#include "Launcher.h"
#include "NavigationManager.h"
#include "EBookmarkManager.h"
#include <cstring>
#include <cstdlib>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdio.h>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

float EReaderApp::s_readingFontSize = 28.0f;

void EReaderApp::setReadingFontSize(float pt) {
    s_readingFontSize = pt;
}

float EReaderApp::getReadingFontSize() {
    return s_readingFontSize;
}

// HTML Helpers
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

static std::string decodeHtmlEntity(const std::string& entity) {
    if (entity == "nbsp") return " ";
    if (entity == "amp") return "&";
    if (entity == "lt") return "<";
    if (entity == "gt") return ">";
    if (entity == "quot") return "\"";
    if (entity == "apos") return "'";
    if (entity == "lsquo" || entity == "rsquo") return "'";
    if (entity == "ldquo" || entity == "rdquo") return "\"";
    if (entity == "mdash" || entity == "ndash") return "-";
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
            if (value == 160) return " ";
            if (value >= 32 && value <= 126) return std::string(1, static_cast<char>(value));
            if (value == 8216 || value == 8217 || value == 39) return "'"; // '
            if (value == 8220 || value == 8221) return "\""; // "
            if (value == 8211 || value == 8212) return "-"; // -
        }
    }
    return " ";
}

static char* stripHTML(const char* html, size_t len, size_t& outLen) {
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
                std::string decoded = decodeHtmlEntity(entity);
                for (char dc : decoded) {
                    appendNormalizedChar(text, dc);
                }
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

static size_t countReadableChars(const char* text, size_t len) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(static_cast<unsigned char>(text[i]))) {
            count++;
        }
    }
    return count;
}

static bool isPreferredEpubChapter(const std::string& chapterPath) {
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

// App Lifecycles
void EReaderApp::onCreate() {
    m_state = STATE_LIB;
    m_libraryPage = 0;
    m_librarySort = LibrarySort::AUTHOR;

    NavigationManager::getInstance().navigateTo(NavTarget{0, STATE_LIB, false, ""});


#ifndef NATIVE_TESTING
    if (!typography.loadFont("/sd/data/Roboto-Regular.ttf", 48.0f)) {
        typography.loadFontFromMemory(data_Roboto_Regular_ttf, data_Roboto_Regular_ttf_len, 48.0f);
    }
#else
    if (!typography.loadFont("data/Roboto-Regular.ttf", 32)) {
        if (!typography.loadFont("Roboto-Regular.ttf", 32)) {
            printf("Failed to load Roboto-Regular.ttf\n");
        }
    }
#endif

    updateLibraryItems();
}

void EReaderApp::onDestroy() {
    if (m_currentBookText) {
        free(m_currentBookText);
        m_currentBookText = nullptr;
        m_currentBookTextLen = 0;
    }
    m_textReader.closeFile();
    m_epubParser.close();
    DisplayHAL::setPortrait(false);
}

void EReaderApp::draw() {
    if (m_state == STATE_LIB) {
        drawLibrary();
    } else {
        drawReading();
    }
}

void EReaderApp::updateLibraryItems() {
    m_parsedLibraryItems.clear();
    m_fileBrowser.setRoot("/books");
    std::vector<FileInfo> allFiles = m_fileBrowser.getFiles();
    m_libraryFiles.clear();

    for (auto& f : allFiles) {
        if (f.isDirectory) continue;
        
        std::string lowerName = f.name;
        for (char& c : lowerName) c = tolower(c);
        if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".epub") {}
        else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".txt") {}
        else if (lowerName.length() >= 3 && lowerName.substr(lowerName.length() - 3) == ".md") {}
        else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".rtf") {}
        else continue;

        m_libraryFiles.push_back(f);

        LibraryItem item;
        item.file = f;
        item.title = f.name;
        item.author = "Unknown Author";
        
        size_t dotPos = item.title.find_last_of('.');
        if (dotPos != std::string::npos) {
            item.title = item.title.substr(0, dotPos);
        }
        
        EBookmarkManager::getInstance().initialize();
        const auto& bmarks = EBookmarkManager::getInstance().getBookmarks();
        for (const auto& b : bmarks) {
            if (b.isbn == item.title) {
                if (!b.title.empty()) item.title = b.title;
                if (!b.author.empty()) item.author = b.author;
                break;
            }
        }
        
        int offset = AppStorage::loadBookmark(AppStorage::toRuntimePath("/books/" + f.name));
        if (offset > 0 && f.size > 0) {
            item.completionPercent = (offset * 100) / (f.size * 2); 
            if (item.completionPercent > 100) item.completionPercent = 100;
            if (item.completionPercent == 0) item.completionPercent = 1;
        } else {
            item.completionPercent = 0;
        }
        
        if (item.completionPercent >= 97) {
            item.completionPercent = -100;
        }
        
        m_parsedLibraryItems.push_back(item);
    }
    
    // Sort
    for (size_t i = 0; i < m_parsedLibraryItems.size(); i++) {
        for (size_t j = i + 1; j < m_parsedLibraryItems.size(); j++) {
            bool swap = false;
            if (m_librarySort == LibrarySort::COMPLETION) {
                swap = m_parsedLibraryItems[j].completionPercent > m_parsedLibraryItems[i].completionPercent;
            } else if (m_librarySort == LibrarySort::AUTHOR) {
                swap = m_parsedLibraryItems[j].author < m_parsedLibraryItems[i].author;
            } else {
                swap = m_parsedLibraryItems[j].title < m_parsedLibraryItems[i].title;
            }
            if (swap) {
                LibraryItem temp = m_parsedLibraryItems[i];
                m_parsedLibraryItems[i] = m_parsedLibraryItems[j];
                m_parsedLibraryItems[j] = temp;
            }
        }
    }
}

void EReaderApp::drawLibrary() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        int w = DisplayHAL::getWidth();
        uint8_t fg = UIFramework::getForegroundColor();

        typography.setFontSize(48.0f);
        typography.renderText("Library", 70, 2, framebuffer, fg);

        typography.setFontSize(28.0f);
        std::string sysInfo = "10:00 AM | 80% | 12GB Free";
        int sysInfoW = typography.measureText(sysInfo);
        typography.renderText(sysInfo, w - sysInfoW - 60, 12, framebuffer, fg);
        UIFramework::drawIcon16x16(framebuffer, 920, 18, COG_ICON, fg);
        DisplayHAL::drawHLine(0, LIB_TOP_H - 1, w, fg, framebuffer);

        DisplayHAL::drawRect(LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H, fg, framebuffer);

        std::vector<std::pair<std::string, std::string>> buttons = {
            {"Sort:", "Author"},
            {"Sort:", "Genre"},
            {"Sort:", "Prog %"},
            {"Exit to", "Launcher"}
        };
        int btnH = LIB_MAIN_H / 4;
        typography.setFontSize(24.0f);
        for (size_t i = 0; i < buttons.size() && i < 4; i++) {
            int by = LIB_MAIN_Y + (i * btnH);
            UIFramework::drawButton(framebuffer, LIB_SIDE_X + 10, by + 10, LIB_SIDE_W - 20, btnH - 20, "");

            int tw1 = typography.measureText(buttons[i].first);
            int tw2 = typography.measureText(buttons[i].second);
            int tx1 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw1) / 2;
            int tx2 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw2) / 2;
            int ty = by + 25;
            typography.renderText(buttons[i].first, tx1, ty, framebuffer, fg);
            typography.renderText(buttons[i].second, tx2, ty + 30, framebuffer, fg);
        }
        typography.setFontSize(32.0f);

        DisplayHAL::fillRect(0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H, 0xDD, framebuffer);

        int pBtnW = LIB_PAGING_W - 20;
        int pBtnH = LIB_MAIN_H / 2 - 20;
        int symbolHeight = 24;

        int upY = LIB_MAIN_Y + 10;
        UIFramework::drawButton(framebuffer, 25, upY, pBtnW, pBtnH, "");
        std::string upSymbol = "/\\";
        int twUp = typography.measureText(upSymbol);
        typography.renderText(upSymbol, 25 + (pBtnW - twUp) / 2, upY + (pBtnH - symbolHeight) / 2, framebuffer, fg);

        int dnY = LIB_MAIN_Y + LIB_MAIN_H / 2 + 10;
        UIFramework::drawButton(framebuffer, 25, dnY, pBtnW, pBtnH, "");
        std::string dnSymbol = "\\/";
        int twDn = typography.measureText(dnSymbol);
        typography.renderText(dnSymbol, 25 + (pBtnW - twDn) / 2, dnY + (pBtnH - symbolHeight) / 2, framebuffer, fg);

        updateLibraryItems();

        int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
        int startIndex = m_libraryPage * itemsPerPage;
        int y = LIB_MAIN_Y;
        typography.setFontSize(40.0f);

        for (int i = 0; i < itemsPerPage && (startIndex + i) < m_parsedLibraryItems.size(); i++) {
            LibraryItem& item = m_parsedLibraryItems[startIndex + i];
            int rowY = y + 20;

            std::string leftText = item.title + " - " + item.author;
            int maxW = LIB_LIST_W - 100;
            while (leftText.length() > 3 && typography.measureText(leftText + "...") > maxW) {
                leftText.pop_back();
            }
            if (leftText.length() < item.title.length() + item.author.length() + 3) {
                leftText += "...";
            }

            std::string rightText = item.completionPercent == -100 ? "100%" : std::to_string(item.completionPercent) + "%";
            int rw = typography.measureText(rightText);
            typography.renderText(leftText, LIB_LIST_X + 20, rowY, framebuffer, fg);
            typography.renderText(rightText, LIB_LIST_X + LIB_LIST_W - rw - 20, rowY, framebuffer, fg);
            y += LIB_ROW_H;
        }

        if (m_parsedLibraryItems.empty()) {
            typography.renderText("No books found.", LIB_LIST_X + 20, LIB_MAIN_Y + 50, framebuffer, fg);
        }
        typography.setFontSize(32.0f);
    });
}

void EReaderApp::drawReading() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();
        uint8_t fg = UIFramework::getForegroundColor();

        prepareTypographyForReading();

        if (m_currentBookText) {
            static bool s_lastPortrait = DisplayHAL::isPortrait();
            static float s_lastFontSize = getReadingFontSize();
            
            if (s_lastPortrait != DisplayHAL::isPortrait() || s_lastFontSize != getReadingFontSize()) {
                m_pageHistory.clear();
                s_lastPortrait = DisplayHAL::isPortrait();
                s_lastFontSize = getReadingFontSize();
            }

            size_t remainingLen = m_currentBookTextLen - m_currentReadingOffset;
            size_t endOffset = typography.findNextPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
            printf("[EReader] Rendering page starting at offset: %d, ending at (next page start): %zu\n", (int)m_currentReadingOffset, endOffset);
            typography.renderText(m_currentBookText + m_currentReadingOffset, remainingLen, 20, 60, framebuffer, fg);
        }

        // Draw [Back] button in top-left bar
        UIFramework::drawButton(framebuffer, 10, 10, 80, 40, "");
        typography.setFontSize(22.0f);
        int btw = typography.measureText("Back");
        typography.renderText("Back", 10 + (80 - btw) / 2, 18, framebuffer, fg);

        std::string infoStr = m_currentBookTitle;
        if (!m_currentBookAuthor.empty() && m_currentBookAuthor != "Unknown Author") {
            infoStr += " - " + m_currentBookAuthor;
        }

        typography.setFontSize(24.0f);
        int maxW = w - 190;
        std::string dispStr = infoStr;
        if (typography.measureText(dispStr) > maxW) {
            while (dispStr.length() > 3 && typography.measureText(dispStr + "...") > maxW) {
                dispStr.pop_back();
            }
            dispStr += "...";
        }
        typography.renderText(dispStr, 100, 20, framebuffer, 0x05);

        typography.setFontSize(32.0f);
        UIFramework::drawButton(framebuffer, w - 80, 10, 70, 40, "");
        int tw = typography.measureText("ROT");
        typography.renderText("ROT", w - 80 + (70 - tw) / 2, 16, framebuffer, fg);

        float progress = 0;
        if (m_currentBookTextLen > 0) {
            if (m_textReader.isOpen()) {
                progress = (float)m_textReader.getPosition() / (float)m_textReader.getFileSize();
            } else {
                progress = (float)m_currentReadingOffset / (float)m_currentBookTextLen;
            }
        }
        if (progress > 1.0f) progress = 1.0f;

        int scrollBarHeight = h * 0.01;
        int scrollBarY = h - h * 0.03;
        int scrollBarX = w * 0.10;

        char progStr[16];
        snprintf(progStr, sizeof(progStr), "%d%%", (int)(progress * 100));
        int pw = typography.measureText(progStr);
        int barEnd = w - w * 0.10 - pw - 10;
        int actualBarWidth = barEnd - scrollBarX;

        if (actualBarWidth > 0) {
            DisplayHAL::drawRect(scrollBarX, scrollBarY, actualBarWidth, scrollBarHeight, fg, framebuffer);
            DisplayHAL::fillRect(scrollBarX, scrollBarY, (int)(actualBarWidth * progress), scrollBarHeight, fg, framebuffer);
            typography.renderText(progStr, barEnd + 10, scrollBarY + scrollBarHeight - 2, framebuffer, fg);
        }
    });
}

void EReaderApp::prepareTypographyForReading() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();
    typography.setTopMargin(60);
    typography.setBottomMargin(h * 0.05);
    typography.setFontSize(getReadingFontSize());
}


void EReaderApp::loadChapter(int index) {
    if (m_chapters.empty() || index < 0 || index >= (int)m_chapters.size()) return;

    m_pageHistory.clear();    
    size_t size = 0;
    char* html = m_epubParser.getFileContent(m_chapters[index], size);
    if (!html) return;
    
    size_t strippedLen = 0;
    char* stripped = stripHTML(html, size, strippedLen);
    free(html);
    
    if (m_currentBookText) {
        free(m_currentBookText);
        m_currentBookText = nullptr;
    }
    
    if (stripped) {
        m_currentBookText = stripped;
        m_currentBookTextLen = strippedLen;
    } else {
        m_currentBookTextLen = 0;
    }
}

void EReaderApp::openBook(int index) {
    if (index < 0 || index >= (int)m_libraryFiles.size()) return;
    
    m_currentBookTitle = "";
    m_currentBookAuthor = "";
    for (const auto& item : m_parsedLibraryItems) {
        if (item.file.path == m_libraryFiles[index].path) {
            m_currentBookTitle = item.title;
            m_currentBookAuthor = item.author;
            break;
        }
    }
    if (m_currentBookTitle.empty()) {
        m_currentBookTitle = m_libraryFiles[index].name;
        size_t dotPos = m_currentBookTitle.find_last_of('.');
        if (dotPos != std::string::npos) {
            m_currentBookTitle = m_currentBookTitle.substr(0, dotPos);
        }
        m_currentBookAuthor = "Unknown Author";
    }
    
    std::string path = AppStorage::toRuntimePath(m_libraryFiles[index].path);
    AppScreens::drawLoading(framebuffer, "Loading...");

    bool isText = false;
    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".txt" || path.substr(path.length() - 4) == ".rtf" || path.substr(path.length() - 4) == ".RTF")) isText = true;
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".md") isText = true;

    if (m_currentBookText) {
        free(m_currentBookText);
        m_currentBookText = nullptr;
        m_currentBookTextLen = 0;
    }

    if (isText) {
        if (m_textReader.openFile(path.c_str())) {
            std::string text = m_textReader.getPageText();
            m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
            m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
            if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
            m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
            if (m_currentBookText) {
                memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
            }
        }
    } else if (m_epubParser.open(path)) {
        m_chapters = m_epubParser.getChapterList();
        m_currentChapterIndex = 0;
        char* fallbackText = nullptr;
        size_t fallbackLen = 0;
        int fallbackIndex = 0;
        for (int i = 0; i < (int)m_chapters.size(); i++) {
            size_t size = 0;
            char* html = m_epubParser.getFileContent(m_chapters[i], size);
            if (!html) continue;

            size_t strippedLen = 0;
            char* stripped = stripHTML(html, size, strippedLen);
            free(html);
            if (!stripped) continue;

            size_t readableChars = countReadableChars(stripped, strippedLen);
            if (isPreferredEpubChapter(m_chapters[i]) && readableChars >= 40) {
                m_currentBookText = stripped;
                m_currentBookTextLen = strippedLen;
                m_currentChapterIndex = i;
                break;
            }
            if (!fallbackText && readableChars > 0) {
                fallbackText = stripped;
                fallbackLen = strippedLen;
                fallbackIndex = i;
            } else {
                free(stripped);
            }
        }
        if (!m_currentBookText && fallbackText) {
            m_currentBookText = fallbackText;
            m_currentBookTextLen = fallbackLen;
            m_currentChapterIndex = fallbackIndex;
        }
    }

    m_currentBookPath = path;
    int savedOffset = AppStorage::loadBookmark(path);

    if (isText && m_textReader.isOpen()) {
        m_textReader.setPosition(savedOffset);
        std::string text = m_textReader.getPageText();
        if (m_currentBookText) free(m_currentBookText);
        m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
        m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
        if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
        m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
        if (m_currentBookText) {
            memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
        }
        m_currentReadingOffset = 0;
    } else {
        m_currentReadingOffset = savedOffset;
        if (m_currentReadingOffset < 0) m_currentReadingOffset = 0;
        if (m_currentReadingOffset > (int)m_currentBookTextLen) m_currentReadingOffset = 0;
        m_pageHistory.clear();
    }

    m_state = STATE_READ;
    NavigationManager::getInstance().navigateTo(NavTarget{0, STATE_READ, DisplayHAL::isPortrait(), path});
    drawReading();
}

void EReaderApp::handleTouch(int x, int y) {
    if (m_state == STATE_LIB) {
        if (x >= 900 && y <= LIB_TOP_H) {
            Launcher::getInstance().switchToApp(2);
            return;
        }
        if (x >= LIB_SIDE_X && y >= LIB_MAIN_Y) {
            int btnIndex = (y - LIB_MAIN_Y) / (LIB_MAIN_H / 4);
            if (btnIndex == 0) m_librarySort = LibrarySort::AUTHOR;
            else if (btnIndex == 1) m_librarySort = LibrarySort::GENRE;
            else if (btnIndex == 2) m_librarySort = LibrarySort::COMPLETION;
            else if (btnIndex == 3) {
                // Exit E-Reader to system Launcher
                onDestroy();
                // External trigger through Singleton
                extern void exitToSystemLauncher();
                exitToSystemLauncher();
                return;
            }
            m_libraryPage = 0;
            drawLibrary();
        } else if (x <= LIB_PAGING_W && y >= LIB_MAIN_Y) {
            if (y < LIB_MAIN_Y + LIB_MAIN_H / 2) {
                if (m_libraryPage > 0) m_libraryPage--;
            } else {
                int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
                int maxPage = m_parsedLibraryItems.size() / itemsPerPage;
                if (m_libraryPage < maxPage) m_libraryPage++;
            }
            drawLibrary();
        } else if (x > LIB_PAGING_W && x < LIB_SIDE_X && y >= LIB_MAIN_Y) {
            int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
            int itemIndex = (y - LIB_MAIN_Y) / LIB_ROW_H;
            int globalIndex = m_libraryPage * itemsPerPage + itemIndex;
            if (globalIndex >= 0 && globalIndex < (int)m_parsedLibraryItems.size()) {
                std::string targetPath = m_parsedLibraryItems[globalIndex].file.path;
                for (size_t i = 0; i < m_libraryFiles.size(); i++) {
                    if (m_libraryFiles[i].path == targetPath) {
                        openBook((int)i);
                        break;
                    }
                }
            }
        }
    } else if (m_state == STATE_READ) {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();
        if (y < 60 && x < 100) {
            if (m_textReader.isOpen()) {
                AppStorage::saveBookmark(m_currentBookPath, m_textReader.getPosition());
                m_textReader.closeFile();
            } else {
                AppStorage::saveBookmark(m_currentBookPath, m_currentReadingOffset);
            }
            if (!NavigationManager::getInstance().goBack()) {
                DisplayHAL::setPortrait(false);
                m_state = STATE_LIB;
                drawLibrary();
            }
        } else if (y < 60 && x > w - 80) {
            DisplayHAL::setPortrait(!DisplayHAL::isPortrait());
            drawReading();
        } else if (x > w / 2) {
            prepareTypographyForReading();
            if (m_textReader.isOpen()) {
                size_t nextStart = typography.findNextPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
                if (nextStart > (size_t)m_currentReadingOffset && nextStart < m_currentBookTextLen) {
                    m_pageHistory.push_back(m_currentReadingOffset);
                    m_currentReadingOffset = nextStart;
                    AppStorage::saveBookmark(m_currentBookPath, m_textReader.getPosition() - (m_currentBookTextLen - m_currentReadingOffset));
                } else {
                    m_pageHistory.clear();
                    m_textReader.nextPage();
                    std::string text = m_textReader.getPageText();
                    if (m_currentBookText) free(m_currentBookText);
                    m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                    m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
                    if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
                    m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
                    if (m_currentBookText) memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
                    AppStorage::saveBookmark(m_currentBookPath, m_textReader.getPosition());
                    m_currentReadingOffset = 0;
                }
            } else {
                size_t nextStart = typography.findNextPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
                if (nextStart > (size_t)m_currentReadingOffset && nextStart < m_currentBookTextLen) {
                    m_pageHistory.push_back(m_currentReadingOffset);
                    m_currentReadingOffset = nextStart;
                } else if (m_currentChapterIndex >= 0 && m_currentChapterIndex + 1 < (int)m_chapters.size()) {
                    m_currentChapterIndex++;
                    loadChapter(m_currentChapterIndex);
                    m_currentReadingOffset = 0;
                }
                AppStorage::saveBookmark(m_currentBookPath, m_currentReadingOffset);
            }
            drawReading();
        } else {
            prepareTypographyForReading();
            if (m_textReader.isOpen()) {
                if (!m_pageHistory.empty()) {
                    m_currentReadingOffset = m_pageHistory.back();
                    m_pageHistory.pop_back();
                } else {
                    m_pageHistory.clear();
                    m_textReader.prevPage();
                    std::string text = m_textReader.getPageText();
                    if (m_currentBookText) free(m_currentBookText);
                    m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                    m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
                    if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
                    m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
                    if (m_currentBookText) {
                        memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
                        m_currentReadingOffset = typography.findPreviousPageStart(m_currentBookText, m_currentBookTextLen, m_currentBookTextLen);
                    }
                    AppStorage::saveBookmark(m_currentBookPath, m_textReader.getPosition());
                }
            } else {
                if (!m_pageHistory.empty()) {
                    m_currentReadingOffset = m_pageHistory.back();
                    m_pageHistory.pop_back();
                } else if (m_currentReadingOffset == 0 && m_currentChapterIndex > 0) {
                    m_currentChapterIndex--;
                    loadChapter(m_currentChapterIndex);
                    m_currentReadingOffset = typography.findPreviousPageStart(m_currentBookText, m_currentBookTextLen, m_currentBookTextLen);
                    m_pageHistory.clear();
                } else {
                    size_t prevStart = typography.findPreviousPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
                    m_currentReadingOffset = prevStart;
                }
                AppStorage::saveBookmark(m_currentBookPath, m_currentReadingOffset);
            }
            drawReading();
        }
    }
}

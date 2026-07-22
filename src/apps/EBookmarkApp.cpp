#include "EBookmarkApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "AppComm.h"
#include "Launcher.h"
#include <ArduinoJson.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <sys/time.h>
#else
#include <stdio.h>
#include <iostream>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

namespace {
uint32_t getCurrentTime() {
#ifndef NATIVE_TESTING
    time_t now;
    time(&now);
    return (uint32_t)now;
#else
    return (uint32_t)time(nullptr);
#endif
}
} // namespace

void EBookmarkApp::onCreate() {
    m_state = STATE_LIST;
    m_ebookmarkPage = 0;
    m_selectedBookmarkIndex = -1;
    m_librarySort = LibrarySort::AUTHOR;
    m_lastLocalUpdateTs = 0;

    EBookmarkManager::getInstance().initialize();
    // Pre-populate read characteristic buffer with loaded database
    EBookmarkManager::getInstance().save();
    m_lastSyncedJson = AppComm::getReadBuffer();

    // When the eBookmark app opens, immediately advertise our local state to
    // the phone so automatic two-way sync can begin.
    if (AppComm::isInitialized()) {
        syncToApp();
    }
}

void EBookmarkApp::onDestroy() {
}

void EBookmarkApp::draw() {
    if (m_state == STATE_LIST) {
        drawEBookmarkLibrary();
    } else {
        drawEBookmarkDetail();
    }
}

void EBookmarkApp::update() {
    if (!AppComm::isInitialized()) return;

    AppComm::poll();
    bool localChanged = false;

    while (AppComm::hasData()) {
        std::string msg = AppComm::getNextMessage();
        if (msg.empty()) continue;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msg);
        if (err) continue;

        std::string type = doc["t"] | "";
        if (type == "BOOK") {
            std::string isbn = doc["isbn"] | "";
            std::string title = doc["title"] | "";
            std::string author = doc["author"] | "";
            std::string genre = doc["genre"] | "";
            int page = doc["page"] | 0;
            int total = doc["total"] | 0;
            uint32_t ts = doc["ts"] | 0;

            if (!isbn.empty()) {
                // Last-change-wins per book: only apply the phone's update if it
                // is newer than the book's most recent local history entry.
                uint32_t localTs = 0;
                for (const auto& book : EBookmarkManager::getInstance().getBookmarks()) {
                    if (book.isbn == isbn && !book.history.empty()) {
                        localTs = book.history.back().timestamp;
                        break;
                    }
                }
                if (ts >= localTs) {
                    EBookmarkManager::getInstance().addOrUpdateBook(isbn, title, author, genre, page, total);
                    if (ts > 0) {
                        EBookmarkManager::getInstance().updatePage(isbn, page, ts);
                    }
                    localChanged = true;
                }
            }
        } else if (type == "GET_BOOKMARKS") {
            syncToApp();
        } else if (type == "TIME") {
            uint32_t val = doc["val"] | 0;
            if (val > 0) {
#ifndef NATIVE_TESTING
                struct timeval tv;
                tv.tv_sec = val;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
#endif
            }
        }
    }

    // If the local database changed (from touch or from an incoming message),
    // push the latest state to the phone's read characteristic.
    if (localChanged) {
        syncToApp();
        draw();
    }
}

void EBookmarkApp::syncToApp() {
    EBookmarkManager::getInstance().save();
    m_lastSyncedJson = AppComm::getReadBuffer();
}

void EBookmarkApp::syncFromApp() {
    // Not used directly; incoming BOOK messages are handled in update().
}

void EBookmarkApp::updateEBookmarkItems() {
    m_sortedEBookmarks = EBookmarkManager::getInstance().getBookmarks();
    for (size_t i = 0; i < m_sortedEBookmarks.size(); i++) {
        for (size_t j = i + 1; j < m_sortedEBookmarks.size(); j++) {
            bool swap = false;
            if (m_librarySort == LibrarySort::COMPLETION) {
                float p1 = m_sortedEBookmarks[i].totalPages > 0 ? (float)m_sortedEBookmarks[i].currentPage / m_sortedEBookmarks[i].totalPages : 0.0f;
                float p2 = m_sortedEBookmarks[j].totalPages > 0 ? (float)m_sortedEBookmarks[j].currentPage / m_sortedEBookmarks[j].totalPages : 0.0f;
                swap = p2 > p1;
            } else if (m_librarySort == LibrarySort::AUTHOR) {
                swap = m_sortedEBookmarks[j].author < m_sortedEBookmarks[i].author;
            } else {
                swap = m_sortedEBookmarks[j].genre < m_sortedEBookmarks[i].genre;
            }
            if (swap) {
                EBookmark temp = m_sortedEBookmarks[i];
                m_sortedEBookmarks[i] = m_sortedEBookmarks[j];
                m_sortedEBookmarks[j] = temp;
            }
        }
    }
}

void EBookmarkApp::drawEBookmarkLibrary() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    typography.setFontSize(32.0f);
    
    UIFramework::clearArea(framebuffer, 0, 0, w, h);
    
    typography.setFontSize(48.0f);
    typography.renderText("eBookmark Library", 70, 2, framebuffer);
    
    typography.setFontSize(28.0f);
    std::string sysInfo = "10:00 AM | 80% | 12GB Free";
    int sysInfoW = typography.measureText(sysInfo);
    typography.renderText(sysInfo, 960 - sysInfoW - 60, 12, framebuffer);
    UIFramework::drawIcon16x16(framebuffer, 920, 18, COG_ICON, 0x00);
    DisplayHAL::drawHLine(0, LIB_TOP_H - 1, 960, 0x00, framebuffer);

    UIFramework::clearArea(framebuffer, LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H);
    DisplayHAL::drawRect(LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H, 0x00, framebuffer);
    
    std::vector<std::pair<std::string, std::string>> buttons = {
        {"Sort:", "Author"},
        {"Sort:", "Genre"},
        {"Sort:", "Prog %"},
        {"Exit to", "Launcher"}
    };
    int btnH = LIB_MAIN_H / 4;
    typography.setFontSize(24.0f);
    for (size_t i = 0; i < buttons.size(); i++) {
        int by = LIB_MAIN_Y + (i * btnH);
        UIFramework::drawButton(framebuffer, LIB_SIDE_X + 10, by + 10, LIB_SIDE_W - 20, btnH - 20, "");
        
        int tw1 = typography.measureText(buttons[i].first);
        int tw2 = typography.measureText(buttons[i].second);
        int tx1 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw1) / 2;
        int tx2 = LIB_SIDE_X + 10 + (LIB_SIDE_W - 20 - tw2) / 2;
        int ty = by + 25;
        typography.renderText(buttons[i].first, tx1, ty, framebuffer);
        typography.renderText(buttons[i].second, tx2, ty + 30, framebuffer);
    }
    typography.setFontSize(32.0f);

    UIFramework::clearArea(framebuffer, 0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H);
    DisplayHAL::fillRect(0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H, 0xDD, framebuffer);

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

    updateEBookmarkItems();
    
    int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
    int startIndex = m_ebookmarkPage * itemsPerPage;
    int y = LIB_MAIN_Y;
    typography.setFontSize(40.0f);
    
    for (int i = 0; i < itemsPerPage && (startIndex + i) < m_sortedEBookmarks.size(); i++) {
        const EBookmark& book = m_sortedEBookmarks[startIndex + i];
        int rowY = y + 20;
        
        std::string leftText = book.title + " - " + book.author;
        int maxW = LIB_LIST_W - 120;
        while (leftText.length() > 3 && typography.measureText(leftText + "...") > maxW) {
            leftText.pop_back();
        }
        if (leftText.length() < book.title.length() + book.author.length() + 3) {
            leftText += "...";
        }
        
        float progress = book.totalPages > 0 ? (float)book.currentPage / book.totalPages : 0.0f;
        std::string rightText = std::to_string((int)(progress * 100)) + "%";
        int rw = typography.measureText(rightText);
        
        typography.renderText(leftText, LIB_LIST_X + 20, rowY, framebuffer);
        typography.renderText(rightText, LIB_LIST_X + LIB_LIST_W - rw - 20, rowY, framebuffer);
        y += LIB_ROW_H;
    }
    
    if (m_sortedEBookmarks.empty()) {
        typography.renderText("No eBookmarks found.", LIB_LIST_X + 20, LIB_MAIN_Y + 50, framebuffer);
    }
    typography.setFontSize(32.0f);
    
    DisplayHAL::display(framebuffer);
}

void EBookmarkApp::drawEBookmarkDetail(bool fullRefresh) {
    if (m_selectedBookmarkIndex < 0 || m_selectedBookmarkIndex >= (int)m_sortedEBookmarks.size()) return;
    const EBookmark& book = m_sortedEBookmarks[m_selectedBookmarkIndex];

    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    if (fullRefresh) {
        DisplayHAL::clear();
        UIFramework::clearArea(framebuffer, 0, 0, w, h);
    }

    auto renderDetailContent = [this, &book, w, h]() {
        int backX = w - 120;
        int backY = 10;
        int backW = 100;
        int backH = 40;
        UIFramework::drawButton(framebuffer, backX, backY, backW, backH, "");
        typography.setFontSize(24.0f);
        int backTextW = typography.measureText("Back");
        typography.renderText("Back", backX + (backW - backTextW) / 2, backY + 8, framebuffer);

        typography.setFontSize(28.0f);
        std::string topText = "eBookmark - " + book.title;
        if (!book.author.empty() && book.author != "Unknown Author") {
            topText += " by " + book.author;
        }
        int maxTopW = backX - 40;
        if (typography.measureText(topText) > maxTopW) {
            while (topText.length() > 3 && typography.measureText(topText + "...") > maxTopW) {
                topText.pop_back();
            }
            topText += "...";
        }
        typography.renderText(topText, 20, 15, framebuffer, 0x05);

        DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

        std::string pageStr = std::to_string(book.currentPage) + " / " + std::to_string(book.totalPages);
        typography.setFontSize(80.0f);
        int pW = typography.measureText(pageStr);
        int pX = (w - pW) / 2;
        int pY = 180;
        typography.renderText(pageStr, pX, pY, framebuffer);

        int btnY1 = 185;
        int btnW1 = 70;
        int btnH1 = 70;
        int minusX = pX - 90;
        int plusX = pX + pW + 20;

        UIFramework::drawButton(framebuffer, minusX, btnY1, btnW1, btnH1, "");
        UIFramework::drawButton(framebuffer, plusX, btnY1, btnW1, btnH1, "");
        
        typography.setFontSize(36.0f);
        int signW1 = typography.measureText("-");
        int signW2 = typography.measureText("+");
        typography.renderText("-", minusX + (btnW1 - signW1) / 2, btnY1 + 16, framebuffer);
        typography.renderText("+", plusX + (btnW1 - signW2) / 2, btnY1 + 16, framebuffer);

        int btnY2 = 270;
        int btnW2 = 70;
        int btnH2 = 50;

        UIFramework::drawButton(framebuffer, minusX, btnY2, btnW2, btnH2, "");
        UIFramework::drawButton(framebuffer, plusX, btnY2, btnW2, btnH2, "");
        
        typography.setFontSize(24.0f);
        int signW3 = typography.measureText("-5");
        int signW4 = typography.measureText("+5");
        typography.renderText("-5", minusX + (btnW2 - signW3) / 2, btnY2 + 12, framebuffer);
        typography.renderText("+5", plusX + (btnW2 - signW4) / 2, btnY2 + 12, framebuffer);

        int days = EBookmarkManager::getInstance().getDaysReading(book);
        float avg = EBookmarkManager::getInstance().getAvgPagesPerSitting(book);
        
        char daysStr[32];
        snprintf(daysStr, sizeof(daysStr), "Days Reading: %d", days);
        
        char avgStr[32];
        snprintf(avgStr, sizeof(avgStr), "Pages/Sit Avg: %.1f", avg);
        
        std::string genreStr = "Genre: " + (book.genre.empty() ? "None" : book.genre);
        
        typography.setFontSize(24.0f);
        typography.renderText(genreStr, 50, 395, framebuffer, 0x03);
        typography.renderText(daysStr, 380, 395, framebuffer, 0x03);
        typography.renderText(avgStr, 700, 395, framebuffer, 0x03);

        int barY = 460;
        int barH = 16;
        int barX = 50;
        int barW = w - 100;
        
        float progress = book.totalPages > 0 ? (float)book.currentPage / book.totalPages : 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        
        DisplayHAL::drawRect(barX, barY, barW, barH, 0x00, framebuffer);
        DisplayHAL::fillRect(barX, barY, (int)(barW * progress), barH, 0x00, framebuffer);

        char percentStr[32];
        snprintf(percentStr, sizeof(percentStr), "%d%% Complete", (int)(progress * 100));
        int percentW = typography.measureText(percentStr);
        typography.renderText(percentStr, (w - percentW) / 2, barY + 25, framebuffer, 0x05);
    };

    if (fullRefresh) {
        renderDetailContent();
        DisplayHAL::display(framebuffer);
    } else {
        UIFramework::perform2PassPartialUpdate(framebuffer, 0, 60, w, h - 60, renderDetailContent);
    }
}

void EBookmarkApp::handleTouch(int x, int y) {
    if (m_state == STATE_LIST) {
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
                // Exit to System Launcher
                onDestroy();
                extern void exitToSystemLauncher();
                exitToSystemLauncher();
                return;
            }
            m_ebookmarkPage = 0;
            drawEBookmarkLibrary();
        } else if (x <= LIB_PAGING_W && y >= LIB_MAIN_Y) {
            if (y < LIB_MAIN_Y + LIB_MAIN_H / 2) {
                if (m_ebookmarkPage > 0) m_ebookmarkPage--;
            } else {
                int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
                if ((m_ebookmarkPage + 1) * itemsPerPage < (int)m_sortedEBookmarks.size()) {
                    m_ebookmarkPage++;
                }
            }
            drawEBookmarkLibrary();
        } else if (x > LIB_PAGING_W && x < LIB_SIDE_X && y >= LIB_MAIN_Y) {
            int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
            int listY = y - LIB_MAIN_Y;
            int clickedRow = listY / LIB_ROW_H;
            int clickedIndex = m_ebookmarkPage * itemsPerPage + clickedRow;
            if (clickedIndex >= 0 && clickedIndex < (int)m_sortedEBookmarks.size()) {
                const auto& books = EBookmarkManager::getInstance().getBookmarks();
                int rawIndex = -1;
                for (size_t i = 0; i < books.size(); i++) {
                    if (books[i].isbn == m_sortedEBookmarks[clickedIndex].isbn) {
                        rawIndex = (int)i;
                        break;
                    }
                }
                if (rawIndex != -1) {
                    m_selectedBookmarkIndex = rawIndex;
                    m_state = STATE_DETAIL;
                    drawEBookmarkDetail(true); // Full clear on entering detail view
                }
            }
        }
    } else if (m_state == STATE_DETAIL) {
        const EBookmark& book = m_sortedEBookmarks[m_selectedBookmarkIndex];
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();

        typography.setFontSize(80.0f);
        std::string pageStr = std::to_string(book.currentPage) + " / " + std::to_string(book.totalPages);
        int pW = typography.measureText(pageStr);
        int pX = (w - pW) / 2;
        int minusX = pX - 90;
        int plusX = pX + pW + 20;
        typography.setFontSize(32.0f);

        if (x >= w - 120 && x <= w - 20 && y >= 10 && y <= 50) {
            m_state = STATE_LIST;
            DisplayHAL::clear(); // Full screen clear to prevent ghosting when returning to library
            memset(framebuffer, 0xFF, w * h / 2);
            drawEBookmarkLibrary();
        } else if (x >= minusX && x <= minusX + 70 && y >= 185 && y <= 255) {
            if (book.currentPage > 0) {
                EBookmarkManager::getInstance().updatePage(book.isbn, book.currentPage - 1);
                m_lastLocalUpdateTs = getCurrentTime();
                updateEBookmarkItems();
                drawEBookmarkDetail(false); // Fast partial update
                syncToApp();
            }
        } else if (x >= plusX && x <= plusX + 70 && y >= 185 && y <= 255) {
            if (book.currentPage < book.totalPages) {
                EBookmarkManager::getInstance().updatePage(book.isbn, book.currentPage + 1);
                m_lastLocalUpdateTs = getCurrentTime();
                updateEBookmarkItems();
                drawEBookmarkDetail(false); // Fast partial update
                syncToApp();
            }
        } else if (x >= minusX && x <= minusX + 70 && y >= 270 && y <= 320) {
            int newPage = book.currentPage - 5;
            if (newPage < 0) newPage = 0;
            EBookmarkManager::getInstance().updatePage(book.isbn, newPage);
            m_lastLocalUpdateTs = getCurrentTime();
            updateEBookmarkItems();
            drawEBookmarkDetail(false); // Fast partial update
            syncToApp();
        } else if (x >= plusX && x <= plusX + 70 && y >= 270 && y <= 320) {
            int newPage = book.currentPage + 5;
            if (newPage > book.totalPages) newPage = book.totalPages;
            EBookmarkManager::getInstance().updatePage(book.isbn, newPage);
            m_lastLocalUpdateTs = getCurrentTime();
            updateEBookmarkItems();
            drawEBookmarkDetail(false); // Fast partial update
            syncToApp();
        }
    }
}

#include "Launcher.h"
#include "EReaderApp.h"
#include "EBookmarkApp.h"
#include "SettingsApp.h"
#include "CalculatorApp.h"
#include "FileManagerApp.h"
#include "ImageViewerApp.h"
#include "TeaRaceApp.h"
#include "KeeperApp.h"
#include "GamesApp.h"
#include "CodebreakerApp.h"
#include "WordSearchApp.h"
#include "NumberPlaceApp.h"
#include "CrevasseApp.h"
#include "KlondikeApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "AppComm.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include "reader/AppStorage.h"
#include <time.h>
#include <sys/time.h>
#include <cstring>
#include <cstdio>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <iostream>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;
void requestSleepNow();

static Application* createEReader() { return new EReaderApp(); }
static Application* createEBookmark() { return new EBookmarkApp(); }
static Application* createSettings() { return new SettingsApp(); }
static Application* createCalculator() { return new CalculatorApp(); }
static Application* createFileManager() { return new FileManagerApp(); }
static Application* createImageViewer() { return new ImageViewerApp(); }
static Application* createTeaRace() { return new TeaRaceApp(); }
static Application* createKeeper() { return new KeeperApp(); }
static Application* createGames() { return new GamesApp(); }
static Application* createCodebreaker() { return new CodebreakerApp(); }
static Application* createWordSearch() { return new WordSearchApp(); }
static Application* createNumberPlace() { return new NumberPlaceApp(); }
static Application* createCrevasse() { return new CrevasseApp(); }
static Application* createKlondike() { return new KlondikeApp(); }

// ---- Home screen layout ----------------------------------------------------
namespace {
using namespace UI;

constexpr int CLOCK_TOP = STATUS_H;
constexpr int CLOCK_H = 150;
constexpr int CARD_Y = 196;
constexpr int CARD_H = 176;
constexpr int GRID_Y = 404;
constexpr int GRID_COLS = 2;
constexpr int GRID_GAP = 16;
constexpr int TILE_W = (W - 2 * MARGIN - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS;
constexpr int TILE_H = 112;

Rect tileRect(int i) {
    int row = i / GRID_COLS;
    int col = i % GRID_COLS;
    return Rect(MARGIN + col * (TILE_W + GRID_GAP), GRID_Y + row * (TILE_H + GRID_GAP), TILE_W, TILE_H);
}

Rect cardRect() { return Rect(MARGIN, CARD_Y, W - 2 * MARGIN, CARD_H); }

// Home grid tiles: apps plus the sleep shortcut.
constexpr int TILE_SLEEP = -3;
const int kTiles[] = {APP_READER, APP_BOOKMARKS, APP_IMAGES, APP_FILES,
                      APP_CALCULATOR, APP_GAMES, APP_SETTINGS, TILE_SLEEP};
constexpr int kTileCount = sizeof(kTiles) / sizeof(kTiles[0]);

void drawClockBlock() {
    time_t raw;
    time(&raw);
    struct tm* t = localtime(&raw);
    bool valid = t && t->tm_year > 100;
    if (valid) {
        char date[48];
        strftime(date, sizeof(date), "%A, %d %B", t);
        text(date, MARGIN, CLOCK_TOP + 8, FONT_BODY, DARK);
        int hr = t->tm_hour % 12;
        if (hr == 0) hr = 12;
        char tm[16];
        snprintf(tm, sizeof(tm), "%d:%02d", hr, t->tm_min);
        int tw = textWidth(tm, 84.0f, true);
        text(tm, MARGIN - 4, CLOCK_TOP + 48, 84.0f, BLACK, true);
        text(t->tm_hour < 12 ? "AM" : "PM", MARGIN + tw + 6, CLOCK_TOP + 106, FONT_BODY, DARK, true);
    } else {
        text("Clock not set", MARGIN, CLOCK_TOP + 8, FONT_BODY, DARK);
        text("Welcome", MARGIN, CLOCK_TOP + 48, FONT_TITLE, BLACK, true);
        text("Set time with the companion app.", MARGIN, CLOCK_TOP + 92, FONT_CAPTION, DARK);
    }
}
} // namespace

Launcher& Launcher::getInstance() {
    static Launcher instance;
    return instance;
}

Launcher::~Launcher() {
    exitCurrentApp();
}

void Launcher::init() {
    m_apps.assign(APP_COUNT, AppDescriptor());
    m_apps[APP_READER]     = {"Library",    "Read EPUB, TXT, MD and RTF books", createEReader, ICON_BOOK};
    m_apps[APP_BOOKMARKS]  = {"Bookmarks",  "Track physical books with your phone", createEBookmark, ICON_BOOKMARK};
    m_apps[APP_IMAGES]     = {"Pictures",   "View JPEG images", createImageViewer, ICON_IMAGE};
    m_apps[APP_FILES]      = {"Files",      "Manage the SD card", createFileManager, ICON_FOLDER};
    m_apps[APP_CALCULATOR] = {"Calculator", "Basic arithmetic", createCalculator, ICON_CALCULATOR};
    m_apps[APP_SETTINGS]   = {"Settings",   "Display, power and Bluetooth", createSettings, ICON_SETTINGS};
    m_apps[APP_TEARACE]    = {"Tea Race",   "Race a clipper from China to London", createTeaRace, ICON_SHIP};
    m_apps[APP_KEEPER]     = {"The Keeper", "Twenty-one nights on a lighthouse", createKeeper, ICON_LIGHTHOUSE};
    m_apps[APP_GAMES]      = {"Games",      "Short games", createGames, ICON_GAMES};
    m_apps[APP_CODEBREAKER] = {"Codebreaker", "Break the enemy cipher, 1917", createCodebreaker, ICON_KEY};
    m_apps[APP_WORDSEARCH] = {"Word Search", "Ring the hidden words", createWordSearch, ICON_GAMES};
    m_apps[APP_NUMBERPLACE] = {"Number Place", "Sudoku, as it began in 1979", createNumberPlace, ICON_GAMES};
    m_apps[APP_CREVASSE]   = {"Crevasse 1911", "Cross the Antarctic ice", createCrevasse, ICON_GAMES};
    m_apps[APP_KLONDIKE]   = {"Klondike 1897", "Patience in a gold-rush cabin", createKlondike, ICON_GAMES};
    m_activeAppIndex = -1;
    m_activeApp = nullptr;
}

void Launcher::loop() {
    processPending();
    if (m_activeApp) {
        m_activeApp->update();
        processPending();
        return;
    }

    // Home screen: refresh just the clock area when the minute changes.
    static int lastMin = -1;
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    int currentMin = timeinfo ? timeinfo->tm_min : -1;
    if (lastMin == -1) {
        lastMin = currentMin;
    } else if (currentMin != lastMin) {
        lastMin = currentMin;
        drawClock();
    }
}

uint32_t Launcher::msUntilUpdate() const {
    if (m_activeApp) return m_activeApp->msUntilUpdate();
    if (m_pendingExit || m_pendingLaunch >= 0 || !m_pendingBook.empty()) return 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint32_t intoMinute = (uint32_t)(tv.tv_sec % 60) * 1000u + (uint32_t)(tv.tv_usec / 1000);
    // A little past the boundary, so loop() sees the new minute.
    return 60000u - intoMinute + 50u;
}

void Launcher::drawClock() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, 0, W, CARD_Y - 8, []() {
        statusBar();
        drawClockBlock();
    });
}

void Launcher::launchApp(int id, bool autoDraw) {
    if (id < 0 || id >= (int)m_apps.size()) return;
    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
    }
    DisplayHAL::setPortrait(true);
    m_activeAppIndex = id;
    m_activeApp = m_apps[id].factory();
    m_activeApp->onCreate();
    if (autoDraw) m_activeApp->draw();
}

void Launcher::exitCurrentApp() {
    if (!m_activeApp) return;
    m_activeApp->onDestroy();
    delete m_activeApp;
    m_activeApp = nullptr;
    m_activeAppIndex = -1;
    drawMenu();
}

void Launcher::requestLaunch(int id, int returnTo) {
    m_pendingLaunch = id;
    m_pendingReturnTo = returnTo;
    m_pendingExit = false;
}

void Launcher::requestExit() {
    m_pendingExit = true;
    m_pendingLaunch = -1;
}

void Launcher::requestOpenBook(const std::string& runtimePath) {
    m_pendingBook = runtimePath;
}

void Launcher::processPending() {
    if (m_pendingExit) {
        m_pendingExit = false;
        if (m_returnTo >= 0) {
            int back = m_returnTo;
            m_returnTo = -1;
            launchApp(back);
        } else {
            exitCurrentApp();
        }
    }
    if (m_pendingLaunch >= 0) {
        int id = m_pendingLaunch;
        m_pendingLaunch = -1;
        m_returnTo = m_pendingReturnTo;
        m_pendingReturnTo = -1;
        launchApp(id);
    }
    if (!m_pendingBook.empty()) {
        std::string path = m_pendingBook;
        m_pendingBook.clear();
        launchApp(APP_READER, false);
        static_cast<EReaderApp*>(m_activeApp)->resumeAtPath(path);
    }
}

void Launcher::drawMenu() {
    DisplayHAL::setPortrait(true);
    AppStorage::LastBook last = AppStorage::loadLastBook();
    m_lastBookPath = last.valid ? last.path : "";

    UIFramework::performFullScreenDraw(framebuffer, [this, &last]() {
        statusBar();
        drawClockBlock();

        // Continue-reading card
        Rect c = cardRect();
        strokeRoundRect(c.x, c.y, c.w, c.h, 18, 2, BLACK);
        icon(ICON_BOOK, c.x + 20, c.y + (c.h - 76) / 2, 76);
        int tx = c.x + 116;
        int tw = c.x + c.w - 24 - tx;
        if (last.valid) {
            text("CONTINUE READING", tx, c.y + 18, FONT_CAPTION, DARK, true);
            int titleBottom = textWrapped(last.title, tx, c.y + 48, tw, FONT_BODY, BLACK, 2, 0);
            std::string author = (last.author.empty() || last.author == "Unknown Author") ? "" : last.author;
            if (!author.empty()) text(ellipsize(author, tw, FONT_SMALL), tx, titleBottom + 2, FONT_SMALL, DARK);
            std::string pct = std::to_string(last.percent) + "%";
            int pw = textWidth(pct, FONT_CAPTION, true);
            progressBar(tx, c.y + c.h - 24, tw - pw - 12, 6, last.percent / 100.0f);
            textRight(pct, tx + tw, c.y + c.h - 34, FONT_CAPTION, INK, true);
        } else {
            text("YOUR LIBRARY", tx, c.y + 30, FONT_CAPTION, DARK, true);
            text("Start reading", tx, c.y + 60, FONT_BODY + 2, BLACK, true);
            textWrapped("Books in /books on the SD card, or send them from the companion app.",
                        tx, c.y + 102, tw, FONT_CAPTION, DARK, 2, 4);
        }

        // Tiles
        for (int i = 0; i < kTileCount; i++) {
            Rect r = tileRect(i);
            UIIconId ic;
            std::string label;
            std::string sub;
            if (kTiles[i] == TILE_SLEEP) {
                ic = ICON_SLEEP;
                label = "Sleep";
            } else {
                ic = m_apps[kTiles[i]].icon;
                label = m_apps[kTiles[i]].name;
            }
            strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, BLACK);
            icon(ic, r.x + 16, r.y + (r.h - 56) / 2, 56);
            int tx = r.x + 86;
            int tw = r.x + r.w - 12 - tx;
            if (sub.empty()) {
                text(ellipsize(label, tw, FONT_BODY - 2, true), tx, r.y + (r.h - lineHeight(FONT_BODY - 2)) / 2,
                     FONT_BODY - 2, BLACK, true);
            } else {
                text(ellipsize(label, tw, FONT_BODY - 2, true), tx, r.y + 28, FONT_BODY - 2, BLACK, true);
                text(sub, tx, r.y + 62, FONT_CAPTION, DARK);
            }
        }

        textCentered("Side button: sleep / wake", W / 2, H - 40, FONT_CAPTION, MID);
    });
}

void Launcher::handleTouch(int x, int y) {
    if (m_activeApp) {
        if (m_activeApp->wantsSystemBack() && hitBack(x, y)) {
            if (!m_activeApp->onBack()) requestExit();
        } else {
            m_activeApp->handleTouch(x, y);
        }
        processPending();
        return;
    }

    if (cardRect().contains(x, y)) {
        if (!m_lastBookPath.empty()) requestOpenBook(m_lastBookPath);
        else requestLaunch(APP_READER);
        processPending();
        return;
    }
    for (int i = 0; i < kTileCount; i++) {
        if (!tileRect(i).contains(x, y)) continue;
        if (kTiles[i] == TILE_SLEEP) {
            requestSleepNow();
        } else {
            launchApp(kTiles[i]);
        }
        return;
    }
}

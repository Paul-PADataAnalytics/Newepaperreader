#include "Launcher.h"
#include "EReaderApp.h"
#include "EBookmarkApp.h"
#include "SettingsApp.h"
#include "CalculatorApp.h"
#include "FileManagerApp.h"
#include "ImageViewerApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "AppComm.h"
#include "ui/UIFramework.h"
#include "app_icons_embedded.h"
#include <time.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdio.h>
#include <iostream>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

Application* createEReader() { return new EReaderApp(); }
Application* createEBookmark() { return new EBookmarkApp(); }
Application* createSettings() { return new SettingsApp(); }
Application* createCalculator() { return new CalculatorApp(); }
Application* createFileManager() { return new FileManagerApp(); }
Application* createImageViewer() { return new ImageViewerApp(); }


static void drawIcon48x48(uint8_t* fb, int iconX, int iconY, const uint8_t* iconData) {
    if (!fb || !iconData) return;
    for (int r = 0; r < 48; r++) {
        for (int c = 0; c < 48; c += 2) {
            uint8_t byteVal = iconData[r * 24 + (c / 2)];
            uint8_t p1 = (byteVal >> 4) & 0x0F;
            uint8_t p2 = byteVal & 0x0F;
            DisplayHAL::setPixel(iconX + c, iconY + r, p1, fb);
            DisplayHAL::setPixel(iconX + c + 1, iconY + r, p2, fb);
        }
    }
}

Launcher& Launcher::getInstance() {
    static Launcher instance;
    return instance;
}

Launcher::~Launcher() {
    exitCurrentApp();
}

void Launcher::init() {
    m_apps = {
        {"eReader App", "Read EPUB and text files from SD card", createEReader},
        {"eBookmark App", "Track physical books via BLE companion", createEBookmark},
        {"Settings App", "Configure system-wide wireless settings", createSettings},
        {"Calculator App", "Simple high-contrast basic math utility", createCalculator},
        {"File Browser", "Manage files and folders on the SD card", createFileManager},
        {"Image Viewer", "JPEG rendering with 16 grayscale conversion", createImageViewer}
    };
    m_activeAppIndex = -1;
    m_activeApp = nullptr;
}

void Launcher::loop() {
    if (AppComm::isInitialized()) {
        AppComm::poll();
    }
    if (m_activeApp) {
        m_activeApp->update();
    } else {
        // Redraw launcher menu if minute changes to keep date/time display updated.
        // NOTE: lastMin must be seeded with the CURRENT minute on the first call
        // (not -1) - otherwise the very first Launcher::loop() call right after
        // boot always sees a "change" and triggers a redundant full redraw of
        // the menu that drawMenu() already just drew moments earlier in setup().
        static int lastMin = -1;
        static bool seeded = false;
        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);
        int currentMin = timeinfo ? timeinfo->tm_min : -1;
        if (!seeded) {
            lastMin = currentMin;
            seeded = true;
        } else if (currentMin != lastMin) {
            lastMin = currentMin;
            drawMenu();
        }
    }
}

void Launcher::launchApp(int index, bool autoDraw) {
    if (index < 0 || index >= (int)m_apps.size()) return;
    exitCurrentApp();
    
    m_previousAppIndex = -1; // Launched directly from main launcher menu
    m_activeAppIndex = index;
    m_activeApp = m_apps[index].factory();
    m_activeApp->onCreate();
    
    // Let the app perform a unified full-screen draw (includes hardware clear).
    // autoDraw=false lets a caller (e.g. deep-sleep breadcrumb restore) drive
    // the app to a specific sub-state first, avoiding a wasted default draw.
    if (autoDraw) {
        m_activeApp->draw();
    }
}

void Launcher::switchToApp(int index) {
    if (index < 0 || index >= (int)m_apps.size()) return;
    int prevIndex = m_activeAppIndex;

    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
    }
    
    m_previousAppIndex = prevIndex;
    m_activeAppIndex = index;
    m_activeApp = m_apps[index].factory();
    m_activeApp->onCreate();
    
    // Let the new app handle its own clear + draw for a single transition refresh.
    m_activeApp->draw();
}

void Launcher::returnToPreviousApp() {
    int prevIndex = m_previousAppIndex;
    if (prevIndex >= 0 && prevIndex < (int)m_apps.size()) {
        switchToApp(prevIndex);
        m_previousAppIndex = -1; // Reset after returning
    } else {
        exitCurrentApp();
    }
}

void Launcher::exitCurrentApp() {
    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
        m_activeAppIndex = -1;
        m_previousAppIndex = -1;
        
        // Return to launcher menu; drawMenu uses performFullScreenDraw which clears.
        drawMenu();
    }
}


void Launcher::drawMenu() {
    // The launcher is always landscape. If an app left portrait mode on, reset it.
    DisplayHAL::setPortrait(false);

    // Unified full-screen draw with mandatory hardware clear — protects E-Ink screen
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();

        // 1. Top Status Bar: Title + Date & Time
        typography.setFontSize(34.0f);
        typography.renderText("LilyGo System v2.3.0", 30, 10, framebuffer);

        // Format current date and time
        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);
        char timeStr[64];
        if (timeinfo && timeinfo->tm_year > 70) {
            strftime(timeStr, sizeof(timeStr), "%a %b %d | %I:%M %p", timeinfo);
        } else {
            snprintf(timeStr, sizeof(timeStr), "No Time Set");
        }

        typography.setFontSize(24.0f);
        typography.renderText(timeStr, 290, 18, framebuffer, 0x03);

        // Render Battery & RAM / System stats on far right
        char statsStr[128];
        int battPct = DisplayHAL::getBatteryPercent();
        const char* chargeIcon = DisplayHAL::isCharging() ? "+" : "";
#ifndef NATIVE_TESTING
        snprintf(statsStr, sizeof(statsStr), "Batt:%d%%%s | Heap:%dKB", 
                 battPct, chargeIcon, (int)(ESP.getFreeHeap() / 1024));
#else
        snprintf(statsStr, sizeof(statsStr), "Batt:%d%%%s | Native", battPct, chargeIcon);
#endif
        typography.setFontSize(18.0f);
        int statsW = typography.measureText(statsStr);
        typography.renderText(statsStr, w - statsW - 20, 22, framebuffer, 0x05); // Grey color


        DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

        // 2. Six Card-style app shortcuts in a 3x2 grid
        struct Card {
            int x, y;
            const char* title;
            const char* desc;
        };

        Card cards[6] = {
            {30, 100, "E-Reader App", "Read EPUB & Text library"},
            {340, 100, "eBookmark", "Track physical reads & stats"},
            {650, 100, "Calculator", "Pocket math & arithmetic"},
            {30, 300, "File Browser", "Manage files on the SD card"},
            {340, 300, "Image Viewer", "Browse & convert JPG images"},
            {650, 300, "Settings App", "Configure wireless settings"}
        };

        int cardW = 280;
        int cardH = 170;

        for (int i = 0; i < 6; i++) {
            UIFramework::drawButton(framebuffer, cards[i].x, cards[i].y, cardW, cardH, "");
            
            // Draw 48x48 App Icon centered at top of card
            int iconX = cards[i].x + (cardW - 48) / 2;
            int iconY = cards[i].y + 15;
            drawIcon48x48(framebuffer, iconX, iconY, g_embeddedAppIcons + (i * 1152));

            // App Title
            typography.setFontSize(24.0f);
            int titleW = typography.measureText(cards[i].title);
            typography.renderText(cards[i].title, cards[i].x + (cardW - titleW) / 2, cards[i].y + 75, framebuffer);
            
            // App Description
            typography.setFontSize(16.0f);
            int descW = typography.measureText(cards[i].desc);
            typography.renderText(cards[i].desc, cards[i].x + (cardW - descW) / 2, cards[i].y + 125, framebuffer, 0x04);
        }

        // Draw bottom helper message
        typography.setFontSize(20.0f);
        std::string help = "Tap the top-left corner (x<=60, y<=60) from any app to return here.";
        int helpW = typography.measureText(help);
        typography.renderText(help, (w - helpW) / 2, h - 35, framebuffer, 0x06);
    });
}


void Launcher::handleTouch(int x, int y) {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    // Global Home corner gesture: if any app is running, and touch is in the top-left (x <= 60, y <= 60) corner, exit app!
    if (m_activeApp) {
        if (x <= 60 && y <= 60) {
#ifndef NATIVE_TESTING
            Serial.println("System Launcher: Escape gesture detected. Exiting app.");
#else
            std::cout << "System Launcher: Escape gesture detected. Exiting app." << std::endl;
#endif
            exitCurrentApp();
            return;
        }
        // Otherwise route touch to active app
        m_activeApp->handleTouch(x, y);
        return;
    }

    // Grid coordinates:
    // Col 0: 30 to 310
    // Col 1: 340 to 620
    // Col 2: 650 to 930
    // Row 0: 100 to 270
    // Row 1: 300 to 470
    int col = -1;
    int row = -1;

    if (y >= 100 && y <= 270) {
        row = 0;
    } else if (y >= 300 && y <= 470) {
        row = 1;
    }

    if (x >= 30 && x <= 310) {
        col = 0;
    } else if (x >= 340 && x <= 620) {
        col = 1;
    } else if (x >= 650 && x <= 930) {
        col = 2;
    }

    if (row != -1 && col != -1) {
        int appIdx = -1;
        if (row == 0) {
            if (col == 0) appIdx = 0;      // E-Reader
            else if (col == 1) appIdx = 1; // E-Bookmark
            else if (col == 2) appIdx = 3; // Calculator
        } else if (row == 1) {
            if (col == 0) appIdx = 4;      // Timer
            else if (col == 1) appIdx = 5; // Image Viewer
            else if (col == 2) appIdx = 2; // Settings
        }

        if (appIdx != -1) {
            launchApp(appIdx);
        }
    }
}

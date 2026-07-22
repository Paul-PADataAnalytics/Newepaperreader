#include "Launcher.h"
#include "EReaderApp.h"
#include "EBookmarkApp.h"
#include "SettingsApp.h"
#include "CalculatorApp.h"
#include "TimerApp.h"
#include "ImageViewerApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "AppComm.h"
#include "ui/UIFramework.h"
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
Application* createTimer() { return new TimerApp(); }
Application* createImageViewer() { return new ImageViewerApp(); }

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
        {"Timer App", "Clock, Stopwatch, and Countdown utility", createTimer},
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
        // Redraw launcher menu if minute changes to keep date/time display updated
        static int lastMin = -1;
        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);
        int currentMin = timeinfo ? timeinfo->tm_min : -1;
        if (currentMin != lastMin) {
            lastMin = currentMin;
            drawMenu();
        }
    }
}

void Launcher::launchApp(int index) {
    if (index < 0 || index >= (int)m_apps.size()) return;
    exitCurrentApp();
    
    m_activeAppIndex = index;
    m_activeApp = m_apps[index].factory();
    m_activeApp->onCreate();
    
    // Clear screen for app launch
    DisplayHAL::clear();
    m_activeApp->draw();
}

void Launcher::switchToApp(int index) {
    if (index < 0 || index >= (int)m_apps.size()) return;
    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
    }
    
    m_activeAppIndex = index;
    m_activeApp = m_apps[index].factory();
    m_activeApp->onCreate();
    
    // Let the new app handle its own clear + draw for a single transition refresh.
    m_activeApp->draw();
}

void Launcher::exitCurrentApp() {
    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
        m_activeAppIndex = -1;
        
        // Return to launcher menu
        DisplayHAL::clear();
        drawMenu();
    }
}

void Launcher::drawMenu() {
    // The launcher is always landscape. If an app left portrait mode on, reset it.
    DisplayHAL::setPortrait(false);

    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // 1. Top Status Bar: Title + Date & Time
    typography.setFontSize(34.0f);
    typography.renderText("LilyGo System v1.02a", 30, 10, framebuffer);

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

    // Render RAM / System stats on far right
    char statsStr[128];
#ifndef NATIVE_TESTING
    snprintf(statsStr, sizeof(statsStr), "Heap:%dKB | PSRAM:%dKB", 
             (int)(ESP.getFreeHeap() / 1024), (int)(ESP.getFreePsram() / 1024));
#else
    snprintf(statsStr, sizeof(statsStr), "Native Mock");
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
        {30, 300, "Timer App", "Clock, Stopwatch, Countdown"},
        {340, 300, "Image Viewer", "Browse & convert JPG images"},
        {650, 300, "Settings App", "Configure wireless settings"}
    };

    int cardW = 280;
    int cardH = 170;

    for (int i = 0; i < 6; i++) {
        UIFramework::drawButton(framebuffer, cards[i].x, cards[i].y, cardW, cardH, "");
        
        typography.setFontSize(30.0f);
        int titleW = typography.measureText(cards[i].title);
        typography.renderText(cards[i].title, cards[i].x + (cardW - titleW) / 2, cards[i].y + 30, framebuffer);
        
        typography.setFontSize(18.0f);
        int descW = typography.measureText(cards[i].desc);
        typography.renderText(cards[i].desc, cards[i].x + (cardW - descW) / 2, cards[i].y + 100, framebuffer, 0x03);
    }

    // Draw bottom helper message
    typography.setFontSize(20.0f);
    std::string help = "Tap the top-left corner (x<=60, y<=60) from any app to return here.";
    int helpW = typography.measureText(help);
    typography.renderText(help, (w - helpW) / 2, h - 35, framebuffer, 0x06);

    DisplayHAL::display(framebuffer);
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

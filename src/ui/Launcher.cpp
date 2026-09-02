#include "Launcher.h"
#include "EReaderApp.h"
#include "EBookmarkApp.h"
#include "SettingsApp.h"
#include "CalculatorApp.h"
#include "FileManagerApp.h"
#include "ImageViewerApp.h"
#include "HeroAdventureApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "AppComm.h"
#include "ui/UIFramework.h"
#include "app_icons_embedded.h"
#include "menu_category_icons.h"
#include <time.h>
#include <cstring>

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
Application* createHeroAdventure() { return new HeroAdventureApp(); }

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
    // Create main menu with categories and direct access apps
    
    // Create Utilities category
    AppDescriptor utilities;
    utilities.name = "Utilities";
    utilities.description = "Access system tools";
    utilities.factory = nullptr;
    utilities.isCategory = true;
    utilities.iconIndex = 0;  // Uses g_menuCategoryIcons[0]
    
    AppDescriptor calc;
    calc.name = "Calculator App";
    calc.description = "Simple high-contrast basic math utility";
    calc.factory = createCalculator;
    calc.isCategory = false;
    calc.iconIndex = 3;
    utilities.items.push_back(calc);
    
    AppDescriptor fileBrowser;
    fileBrowser.name = "File Browser";
    fileBrowser.description = "Manage files and folders on the SD card";
    fileBrowser.factory = createFileManager;
    fileBrowser.isCategory = false;
    fileBrowser.iconIndex = 4;
    utilities.items.push_back(fileBrowser);
    
    AppDescriptor settings;
    settings.name = "Settings App";
    settings.description = "Configure system-wide wireless settings";
    settings.factory = createSettings;
    settings.isCategory = false;
    settings.iconIndex = 5;
    utilities.items.push_back(settings);
    
    // Create Games category
    AppDescriptor games;
    games.name = "Games";
    games.description = "Play adventures";
    games.factory = nullptr;
    games.isCategory = true;
    games.iconIndex = 1;  // Uses g_menuCategoryIcons[1]
    
    AppDescriptor heroAdventure;
    heroAdventure.name = "Hero Adventure";
    heroAdventure.description = "Epic text-based RPG with touch controls";
    heroAdventure.factory = createHeroAdventure;
    heroAdventure.isCategory = false;
    heroAdventure.iconIndex = 7;
    games.items.push_back(heroAdventure);
    
    // Build main menu
    m_apps.clear();
    
    AppDescriptor reader;
    reader.name = "eReader App";
    reader.description = "Read EPUB and text files from SD card";
    reader.factory = createEReader;
    reader.isCategory = false;
    reader.iconIndex = 0;
    m_apps.push_back(reader);
    
    AppDescriptor bookmark;
    bookmark.name = "eBookmark App";
    bookmark.description = "Track physical books via BLE companion";
    bookmark.factory = createEBookmark;
    bookmark.isCategory = false;
    bookmark.iconIndex = 1;
    m_apps.push_back(bookmark);
    
    AppDescriptor imageViewer;
    imageViewer.name = "Image Viewer";
    imageViewer.description = "JPEG rendering with 16 grayscale conversion";
    imageViewer.factory = createImageViewer;
    imageViewer.isCategory = false;
    imageViewer.iconIndex = 2;
    m_apps.push_back(imageViewer);
    
    m_apps.push_back(utilities);
    m_apps.push_back(games);
    
    m_currentMenu = &m_apps;
    m_currentCategoryIndex = -1;
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
    if (index < 0 || index >= (int)m_currentMenu->size()) return;
    
    AppDescriptor& app = (*m_currentMenu)[index];
    
    // If it's a category, navigate into it
    if (app.isCategory) {
        m_currentCategoryIndex = index;
        m_currentMenu = &app.items;
        drawMenu();
        return;
    }
    
    // Otherwise it's a real app - launch it
    exitCurrentApp();
    m_previousAppIndex = m_currentCategoryIndex;
    m_activeAppIndex = index;
    m_activeApp = app.factory();
    m_activeApp->onCreate();
    
    if (autoDraw) {
        m_activeApp->draw();
    }
}

void Launcher::switchToApp(int index) {
    if (index < 0 || index >= (int)m_currentMenu->size()) return;
    
    AppDescriptor& app = (*m_currentMenu)[index];
    
    // If it's a category, navigate into it
    if (app.isCategory) {
        m_currentCategoryIndex = index;
        m_currentMenu = &app.items;
        drawMenu();
        return;
    }
    
    int prevIndex = m_activeAppIndex;

    if (m_activeApp) {
        m_activeApp->onDestroy();
        delete m_activeApp;
        m_activeApp = nullptr;
    }
    
    m_previousAppIndex = prevIndex;
    m_activeAppIndex = index;
    m_activeApp = app.factory();
    m_activeApp->onCreate();
    m_activeApp->draw();
}

void Launcher::returnToPreviousApp() {
    int prevIndex = m_previousAppIndex;
    if (prevIndex >= 0) {
        // If we're currently in a submenu, go back to main menu first
        if (m_currentCategoryIndex >= 0) {
            m_currentMenu = &m_apps;
            m_currentCategoryIndex = -1;
            drawMenu();
        } else {
            // Otherwise switch to previous app
            switchToApp(prevIndex);
            m_previousAppIndex = -1;
        }
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
        
        // Return to main launcher menu
        m_currentMenu = &m_apps;
        m_currentCategoryIndex = -1;
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

        // 1. Top Status Bar: Title + shared live Date/Time/Battery status
        typography.setFontSize(34.0f);
        const char* menuTitle = (m_currentCategoryIndex >= 0 && m_currentCategoryIndex < (int)m_apps.size()) ? 
            m_apps[m_currentCategoryIndex].name.c_str() :
            "LilyGo System v2.3.0";
        typography.renderText(menuTitle, 30, 10, framebuffer);

        UIFramework::drawSystemStatus(framebuffer, w - 20, 18, 24.0f, 0x03, /*includeDate=*/true);

        // Render extra Heap / native build stat on its own line beneath the
        // shared status (heap is a Launcher-only debug stat, not part of the
        // common system status service).
        char heapStr[64];
#ifndef NATIVE_TESTING
        snprintf(heapStr, sizeof(heapStr), "Heap:%dKB", (int)(ESP.getFreeHeap() / 1024));
#else
        snprintf(heapStr, sizeof(heapStr), "Native");
#endif
        typography.setFontSize(16.0f);
        int heapW = typography.measureText(heapStr);
        typography.renderText(heapStr, w - heapW - 20, 42, framebuffer, 0x05); // Grey color

        DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

        // 2. Draw menu items (main or category)
        int itemCount = m_currentMenu->size();
        
        struct Card {
            int x, y;
            std::string title;
            std::string desc;
            int iconIndex;
            bool isCategory;
        };
        
        // Create card layout
        std::vector<Card> cards;
        for (int i = 0; i < itemCount && (int)cards.size() < 6; i++) {
            const AppDescriptor& app = (*m_currentMenu)[i];
            int row = i / 3;
            int col = i % 3;
            int cardX = 30 + col * 310;
            int cardY = 100 + row * 180;
            
            cards.push_back({
                cardX, cardY,
                app.name,
                app.description,
                app.iconIndex,
                app.isCategory
            });
        }
        
        int cardW = 280;
        int cardH = 170;

        // Draw all cards
        for (size_t i = 0; i < cards.size(); i++) {
            const Card& card = cards[i];
            UIFramework::drawButton(framebuffer, card.x, card.y, cardW, cardH, "");
            
            // Draw 48x48 App Icon centered at top of card
            int iconX = card.x + (cardW - 48) / 2;
            int iconY = card.y + 15;
            
            const uint8_t* iconData = nullptr;
            if (card.isCategory) {
                // Use category icons from g_menuCategoryIcons
                if (card.iconIndex < 2) {
                    iconData = g_menuCategoryIcons + (card.iconIndex * 1152);
                }
            } else {
                // Use regular app icons from g_embeddedAppIcons
                if (card.iconIndex < 8) {
                    iconData = g_embeddedAppIcons + (card.iconIndex * 1152);
                }
            }
            
            if (iconData) {
                drawIcon48x48(framebuffer, iconX, iconY, iconData);
            }

            // App Title
            typography.setFontSize(24.0f);
            int titleW = typography.measureText(card.title.c_str());
            typography.renderText(card.title.c_str(), card.x + (cardW - titleW) / 2, card.y + 75, framebuffer);
            
            // App Description
            typography.setFontSize(16.0f);
            int descW = typography.measureText(card.desc.c_str());
            typography.renderText(card.desc.c_str(), card.x + (cardW - descW) / 2, card.y + 125, framebuffer, 0x04);
        }

        // Draw footer with navigation hint
        typography.setFontSize(20.0f);
        std::string help;
        if (m_currentCategoryIndex >= 0) {
            help = "Tap the top-left corner to go back to main menu.";
        } else {
            help = "Tap the top-left corner (x<=60, y<=60) from any app to return here.";
        }
        int helpW = typography.measureText(help.c_str());
        typography.renderText(help.c_str(), (w - helpW) / 2, h - 35, framebuffer, 0x06);
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
    
    // If we're in a submenu and touch top-left, go back to main menu
    if (m_currentCategoryIndex >= 0 && x <= 60 && y <= 60) {
        m_currentMenu = &m_apps;
        m_currentCategoryIndex = -1;
        drawMenu();
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
            if (col == 0) appIdx = 0;
            else if (col == 1) appIdx = 1;
            else if (col == 2) appIdx = 2;
        } else if (row == 1) {
            if (col == 0) appIdx = 3;
            else if (col == 1) appIdx = 4;
            else if (col == 2) appIdx = 5;
        }

        if (appIdx != -1 && appIdx < (int)m_currentMenu->size()) {
            launchApp(appIdx);
        }
    }
}

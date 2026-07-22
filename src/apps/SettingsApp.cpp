#include "SettingsApp.h"
#include "EReaderApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "AppComm.h"
#include "EBookmarkManager.h"
#include <ArduinoJson.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <sys/time.h>
#include <base64.h>
#else
#include <stdio.h>
#include <iostream>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

// Region occupied by the status/log line
static constexpr int LOG_X = 100;
static constexpr int LOG_Y = 355;
static constexpr int LOG_H = 30;

namespace {
// Decode a base64 string into bytes. Works on native builds; on ESP32 the
// Arduino core's base64.h is preferred but this fallback is always available.
std::vector<uint8_t> decodeBase64(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> t(256, -1);
    for (int i = 0; i < 64; ++i) t[(unsigned char)table[i]] = i;

    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (t[c] == -1) continue;
        val = (val << 6) + t[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(uint8_t((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}
} // namespace

void SettingsApp::onCreate() {
    m_bleActive = AppComm::isInitialized();
    m_lastDrawnBleActive = m_bleActive;
    m_syncStatus = m_bleActive ? "Ready for connection" : "BLE Radio is Off";
    m_lastDrawnStatus = m_syncStatus;
    m_processingTouch = false;
    m_hasDrawn = false;
}

void SettingsApp::onDestroy() {
    // Keep BLE running if active so they can continue syncing, or we can close it on exit.
    // Standard approach: leave it as the user toggled it.
}

void SettingsApp::draw() {
    m_bleActive = AppComm::isInitialized();
    bool stateChanged = (m_bleActive != m_lastDrawnBleActive);
    bool statusChanged = (m_syncStatus != m_lastDrawnStatus);
    bool pageChanged = (m_page != m_lastDrawnPage);

    if (pageChanged || !m_hasDrawn) {
        DisplayHAL::clear();
        if (m_page == PAGE_MAIN) {
            drawMainSettings();
        } else if (m_page == PAGE_BLE) {
            drawBleSettings(false);
        } else if (m_page == PAGE_EREADER) {
            drawEReaderSettings();
        }
        m_hasDrawn = true;
    } else if (m_page == PAGE_BLE && (stateChanged || statusChanged)) {
        if (stateChanged) {
            drawBleSettings(true);
        } else {
            drawLogLineOnly();
        }
    } else if (m_page == PAGE_MAIN && (stateChanged || statusChanged)) {
        drawMainSettings();
    }

    m_lastDrawnBleActive = m_bleActive;
    m_lastDrawnStatus = m_syncStatus;
    m_lastDrawnPage = m_page;
}

void SettingsApp::drawLogLineOnly() {
    int w = DisplayHAL::getWidth();
    UIFramework::clearArea(framebuffer, 0, 280, w, 80);

    typography.setFontSize(20.0f);
    if (m_bleActive) {
        typography.renderText("Advertising BLE Device: EPD-Reader", 100, 300, framebuffer, 0x03);
    }
    typography.renderText("Log: " + m_syncStatus, LOG_X, LOG_Y, framebuffer, 0x04);

    DisplayHAL::display(framebuffer);
}

void SettingsApp::update() {
    m_bleActive = AppComm::isInitialized();
    if (m_bleActive) {
        AppComm::poll();
    }
}

void SettingsApp::drawMainSettings() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // 1. Header Title
    typography.setFontSize(40.0f);
    typography.renderText("System Settings", 70, 10, framebuffer);
    DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

    // 2. Line 1: BLE Settings: Connected/Disconnected  [Open BLE Settings]
    int line1Y = 130;
    std::string bleStateStr = m_bleActive ? "Connected" : "Disconnected";
    uint8_t bleColor = m_bleActive ? 0x00 : 0x06;
    
    typography.setFontSize(26.0f);
    typography.renderText("BLE Settings:", 70, line1Y + 10, framebuffer);
    typography.renderText(bleStateStr, 250, line1Y + 10, framebuffer, bleColor);

    int btnW = 280;
    int btnH = 50;
    int btn1X = 600;
    int btn1Y = line1Y;

    UIFramework::drawButton(framebuffer, btn1X, btn1Y, btnW, btnH, "");
    typography.setFontSize(22.0f);
    int tw1 = typography.measureText("Open BLE Settings");
    typography.renderText("Open BLE Settings", btn1X + (btnW - tw1) / 2, btn1Y + 14, framebuffer);

    DisplayHAL::drawHLine(50, 210, w - 100, 0x03, framebuffer);

    // 3. Line 2: Light/Dark Mode: Light Mode / Dark Mode  [Toggle Mode]
    int line2Y = 240;
    std::string modeStateStr = DisplayHAL::isDarkMode() ? "Dark Mode" : "Light Mode";

    typography.setFontSize(26.0f);
    typography.renderText("Light/Dark Mode:", 70, line2Y + 10, framebuffer);
    typography.renderText(modeStateStr, 310, line2Y + 10, framebuffer);

    int btn2X = 600;
    int btn2Y = line2Y;

    UIFramework::drawButton(framebuffer, btn2X, btn2Y, btnW, btnH, "");
    typography.setFontSize(22.0f);
    int tw2 = typography.measureText("Toggle Mode");
    typography.renderText("Toggle Mode", btn2X + (btnW - tw2) / 2, btn2Y + 14, framebuffer);

    DisplayHAL::drawHLine(50, 310, w - 100, 0x03, framebuffer);

    // 4. Line 3: E-Reader Settings: Font Size XXpt  [Open E-Reader Settings]
    int line3Y = 350;
    int currentFontSize = (int)EReaderApp::getReadingFontSize();
    std::string ereaderText = "E-Reader Settings: Font Size " + std::to_string(currentFontSize) + "pt";

    typography.setFontSize(26.0f);
    typography.renderText(ereaderText, 70, line3Y + 10, framebuffer);

    int btn3X = 600;
    int btn3Y = line3Y;

    UIFramework::drawButton(framebuffer, btn3X, btn3Y, btnW, btnH, "");
    typography.setFontSize(22.0f);
    int tw3 = typography.measureText("Open E-Reader Settings");
    typography.renderText("Open E-Reader Settings", btn3X + (btnW - tw3) / 2, btn3Y + 14, framebuffer);

    DisplayHAL::drawHLine(50, 420, w - 100, 0x03, framebuffer);

    // 5. Exit Settings Button
    int exitX = (w - 240) / 2;
    int exitY = 445;
    int exitW = 240;
    int exitH = 50;
    UIFramework::drawButton(framebuffer, exitX, exitY, exitW, exitH, "");
    typography.setFontSize(24.0f);
    int btw = typography.measureText("Exit Settings");
    typography.renderText("Exit Settings", exitX + (exitW - btw) / 2, exitY + 12, framebuffer);

    DisplayHAL::display(framebuffer);
}

void SettingsApp::drawBleSettings(bool forceFullRefresh) {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    if (forceFullRefresh) {
        DisplayHAL::clear();
    }

    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // Header Title & Back Button
    typography.setFontSize(40.0f);
    typography.renderText("BLE Settings", 70, 10, framebuffer);

    int backX = w - 120;
    int backY = 10;
    int backW = 100;
    int backH = 40;
    UIFramework::drawButton(framebuffer, backX, backY, backW, backH, "");
    typography.setFontSize(24.0f);
    int btw = typography.measureText("Back");
    typography.renderText("Back", backX + (backW - btw) / 2, backY + 8, framebuffer);

    DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

    // BLE Toggle Card
    int cardX = 100;
    int cardY = 110;
    int cardW = w - 200;
    int cardH = 160;

    UIFramework::drawButton(framebuffer, cardX, cardY, cardW, cardH, "");

    typography.setFontSize(28.0f);
    typography.renderText("BLE Sync Server Broadcast", cardX + 30, cardY + 30, framebuffer);

    std::string stateStr = m_bleActive ? "ENABLED (ON)" : "DISABLED (OFF)";
    uint8_t stateColor = m_bleActive ? 0x00 : 0x06;
    typography.setFontSize(24.0f);
    typography.renderText("Status: " + stateStr, cardX + 30, cardY + 90, framebuffer, stateColor);

    int btnW = 160;
    int btnH = 60;
    int btnX = cardX + cardW - btnW - 30;
    int btnY = cardY + (cardH - btnH) / 2;

    UIFramework::drawButton(framebuffer, btnX, btnY, btnW, btnH, "", m_bleActive);
    
    typography.setFontSize(22.0f);
    std::string btnText = m_bleActive ? "Turn OFF" : "Turn ON";
    int tw = typography.measureText(btnText);
    typography.renderText(btnText, btnX + (btnW - tw) / 2, btnY + 18, framebuffer, m_bleActive ? 0xFF : 0x00);

    // Broadcast info & Log output
    typography.setFontSize(20.0f);
    if (m_bleActive) {
        typography.renderText("Advertising BLE Device: EPD-Reader", 100, 300, framebuffer, 0x03);
    }
    typography.renderText("Log: " + m_syncStatus, LOG_X, LOG_Y, framebuffer, 0x04);

    DisplayHAL::display(framebuffer);
}

void SettingsApp::drawEReaderSettings() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // Header Title & Back Button
    typography.setFontSize(40.0f);
    typography.renderText("E-Reader Settings", 70, 10, framebuffer);

    int backX = 820;
    int backY = 10;
    int backW = 100;
    int backH = 40;
    UIFramework::drawButton(framebuffer, backX, backY, backW, backH, "");
    typography.setFontSize(24.0f);
    int btw = typography.measureText("Back");
    typography.renderText("Back", backX + (backW - btw) / 2, backY + 8, framebuffer);

    DisplayHAL::drawHLine(0, 60, w, 0x00, framebuffer);

    // Current Font Size Display
    float curSize = EReaderApp::getReadingFontSize();
    typography.setFontSize(28.0f);
    std::string currentStr = "Reading Font Size: " + std::to_string((int)curSize) + "pt";
    typography.renderText(currentStr, 70, 100, framebuffer);

    // Controls Row 1: [-] and [+] Step Buttons
    // [-] button at x=70, y=160, w=100, h=60
    // [+] button at x=200, y=160, w=100, h=60
    UIFramework::drawButton(framebuffer, 70, 160, 100, 60, "");
    typography.setFontSize(36.0f);
    int minusW = typography.measureText("-");
    typography.renderText("-", 70 + (100 - minusW) / 2, 160 + 10, framebuffer);

    UIFramework::drawButton(framebuffer, 200, 160, 100, 60, "");
    int plusW = typography.measureText("+");
    typography.renderText("+", 200 + (100 - plusW) / 2, 160 + 10, framebuffer);

    // Section Label: Preset Font Sizes
    typography.setFontSize(26.0f);
    typography.renderText("Presets:", 70, 260, framebuffer);

    // Controls Row 2: Preset buttons (20pt, 24pt, 28pt, 32pt, 36pt, 40pt)
    static const int presets[] = {20, 24, 28, 32, 36, 40};
    int presetBtnW = 110;
    int presetBtnH = 55;
    int startX = 70;
    int startY = 310;
    int gapX = 25;

    for (int i = 0; i < 6; i++) {
        int px = startX + i * (presetBtnW + gapX);
        int py = startY;
        bool isSelected = ((int)curSize == presets[i]);

        UIFramework::drawButton(framebuffer, px, py, presetBtnW, presetBtnH, "", isSelected);

        typography.setFontSize(24.0f);
        std::string pStr = std::to_string(presets[i]) + "pt";
        int pw = typography.measureText(pStr);
        uint8_t fontColor = isSelected ? 0xFF : 0x00;
        typography.renderText(pStr, px + (presetBtnW - pw) / 2, py + 14, framebuffer, fontColor);
    }

    DisplayHAL::display(framebuffer);
}

void SettingsApp::handleTouch(int x, int y) {
    int w = DisplayHAL::getWidth();

    if (m_page == PAGE_MAIN) {
        // Row 1: Open BLE Settings button (x=600..880, y=130..180)
        if (x >= 600 && x <= 880 && y >= 130 && y <= 180) {
            m_page = PAGE_BLE;
            draw();
            return;
        }

        // Row 2: Toggle Mode button (x=600..880, y=240..290)
        if (x >= 600 && x <= 880 && y >= 240 && y <= 290) {
            DisplayHAL::setDarkMode(!DisplayHAL::isDarkMode());
            draw();
            return;
        }

        // Row 3: Open E-Reader Settings button (x=600..880, y=350..400)
        if (x >= 600 && x <= 880 && y >= 350 && y <= 400) {
            m_page = PAGE_EREADER;
            draw();
            return;
        }

        // Exit Settings button (x=360..600, y=445..495)
        int exitX = (w - 240) / 2;
        int exitY = 445;
        int exitW = 240;
        int exitH = 50;
        if (x >= exitX && x <= exitX + exitW && y >= exitY && y <= exitY + exitH) {
            extern void exitToSystemLauncher();
            exitToSystemLauncher();
            return;
        }
    } else if (m_page == PAGE_BLE) {
        // Back button (top right x=w-120..w-20, y=10..50)
        if (x >= w - 120 && x <= w - 20 && y >= 10 && y <= 50) {
            m_page = PAGE_MAIN;
            draw();
            return;
        }

        // BLE Toggle button inside Card
        int cardX = 100;
        int cardY = 110;
        int cardW = w - 200;
        int cardH = 160;
        int btnW = 160;
        int btnH = 60;
        int btnX = cardX + cardW - btnW - 30;
        int btnY = cardY + (cardH - btnH) / 2;

        if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
            if (m_processingTouch) return;
            m_processingTouch = true;

            if (m_bleActive) {
                AppComm::deinit();
                m_syncStatus = "BLE Sync disabled";
            } else {
                AppComm::init();
                if (AppComm::isInitialized()) {
                    m_syncStatus = "Broadcasting BLE sync...";
                } else {
                    m_syncStatus = "BLE init failed";
                }
            }
            m_bleActive = AppComm::isInitialized();
            draw();

            m_processingTouch = false;
            return;
        }
    } else if (m_page == PAGE_EREADER) {
        // Back button (top right x=820..920, y=10..50)
        if (x >= 820 && x <= 920 && y >= 10 && y <= 50) {
            m_page = PAGE_MAIN;
            draw();
            return;
        }

        // [-] step button (x=70..170, y=160..220)
        if (x >= 70 && x <= 170 && y >= 160 && y <= 220) {
            float cur = EReaderApp::getReadingFontSize();
            if (cur > 12.0f) {
                EReaderApp::setReadingFontSize(cur - 2.0f);
                draw();
            }
            return;
        }

        // [+] step button (x=200..300, y=160..220)
        if (x >= 200 && x <= 300 && y >= 160 && y <= 220) {
            float cur = EReaderApp::getReadingFontSize();
            if (cur < 60.0f) {
                EReaderApp::setReadingFontSize(cur + 2.0f);
                draw();
            }
            return;
        }

        // Preset buttons (x=70 + i*135 .. + 110, y=310..365)
        static const int presets[] = {20, 24, 28, 32, 36, 40};
        int presetBtnW = 110;
        int presetBtnH = 55;
        int startX = 70;
        int startY = 310;
        int gapX = 25;

        for (int i = 0; i < 6; i++) {
            int px = startX + i * (presetBtnW + gapX);
            int py = startY;
            if (x >= px && x <= px + presetBtnW && y >= py && y <= py + presetBtnH) {
                EReaderApp::setReadingFontSize((float)presets[i]);
                draw();
                return;
            }
        }
    }
}

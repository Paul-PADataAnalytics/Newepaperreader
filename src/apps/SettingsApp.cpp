#include "SettingsApp.h"
#include "EReaderApp.h"
#include "DisplayHAL.h"
#include "AppComm.h"
#include "PowerManager.h"
#include "SystemSettings.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <cstdio>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#else
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

extern uint8_t* framebuffer;
void requestSleepNow();

namespace {
using namespace UI;

const Rect kTabs(MARGIN, HEADER_H + 16, W - 2 * MARGIN, 52);
constexpr int CONTENT_Y = HEADER_H + 92;
constexpr int ROW = 104;

const int kSleepOptions[] = {1, 2, 5, 10, 30, 0};
constexpr int kSleepOptionCount = 6;

// Display tab
Rect darkToggle() { return Rect(W - MARGIN - TOGGLE_W, CONTENT_Y + (ROW - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H); }
Rect fontMinus() { return Rect(W - MARGIN - 210, CONTENT_Y + ROW + 22, 70, 58); }
Rect fontPlus() { return Rect(W - MARGIN - 70, CONTENT_Y + ROW + 22, 70, 58); }
Rect brightnessSeg() { return Rect(MARGIN, CONTENT_Y + 2 * ROW + 76, W - 2 * MARGIN, 54); }

// Power tab
Rect sleepSeg() { return Rect(MARGIN, CONTENT_Y + 76, W - 2 * MARGIN, 54); }
constexpr int POWER_ROW2 = CONTENT_Y + 150;
Rect usbToggle() { return Rect(W - MARGIN - TOGGLE_W, POWER_ROW2 + (ROW - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H); }
Rect alwaysToggle() { return Rect(W - MARGIN - TOGGLE_W, POWER_ROW2 + ROW + (ROW - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H); }
constexpr int POWER_ROW4 = POWER_ROW2 + 2 * ROW;
Rect sleepScreenSeg() { return Rect(MARGIN, POWER_ROW4 + 50, W - 2 * MARGIN, 54); }
constexpr int POWER_BATT = POWER_ROW4 + 130;
Rect sleepNowBtn() { return Rect(MARGIN, H - 100, W - 2 * MARGIN, 64); }

// Bluetooth tab
Rect bleToggle() { return Rect(W - MARGIN - TOGGLE_W, CONTENT_Y + (ROW - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H); }
constexpr int BLE_CARD_Y = CONTENT_Y + ROW + 16;
constexpr int BLE_CARD_H = 200;

int sleepOptionIndex(int minutes) {
    for (int i = 0; i < kSleepOptionCount; i++) if (kSleepOptions[i] == minutes) return i;
    return 2;
}

void fontSizeRow(int y) {
    float size = EReaderApp::getReadingFontSize();
    settingLabel(y, ROW, "Text size", "Font size for books");
    Rect m = fontMinus(), p = fontPlus();
    button(m, "A-", ButtonStyle::SECONDARY, FONT_SMALL, size > 16.0f);
    button(p, "A+", ButtonStyle::SECONDARY, FONT_BODY, size < 48.0f);
    textCentered(std::to_string((int)size), (m.x + m.w + p.x) / 2, y + (ROW - lineHeight(FONT_BODY)) / 2, FONT_BODY, BLACK, true);
}
} // namespace

bool SettingsApp::s_openBluetooth = false;

void SettingsApp::onCreate() {
    m_tab = s_openBluetooth ? TAB_BLUETOOTH : TAB_DISPLAY;
    s_openBluetooth = false;
    m_lastBleStatus = bleStatusText();
}

void SettingsApp::onDestroy() {}

std::string SettingsApp::bleStatusText() const {
    if (!AppComm::isInitialized()) return "Off";
    std::string s = AppComm::isConnected() ? "Connected" : "Waiting for phone";
    if (AppComm::isTransferActive()) {
        s += " | Receiving " + AppComm::transferName() + " " + std::to_string(AppComm::transferPercent()) + "%";
    }
    s += " | " + AppComm::lastEvent();
    return s;
}

void SettingsApp::drawBleStatusCard(int y) {
    strokeRoundRect(MARGIN, y, W - 2 * MARGIN, BLE_CARD_H, 16, 2, BLACK);
    int x = MARGIN + 24;
    int w = W - 2 * MARGIN - 48;
    if (!AppComm::isInitialized()) {
        text("Bluetooth is off", x, y + 24, FONT_BODY, BLACK, true);
        textWrapped("Turn it on to sync bookmarks and send books from the companion app.", x, y + 70, w, FONT_SMALL, DARK, 3);
        return;
    }
    bool connected = AppComm::isConnected();
    icon(ICON_BLUETOOTH, x - 6, y + 18, 40, connected ? BLACK : MID);
    text(connected ? "Phone connected" : "Ready to connect", x + 42, y + 24, FONT_BODY, BLACK, true);
    text(connected ? "Companion app is linked" : "Visible as \"EPD-Reader\"", x, y + 72, FONT_SMALL, DARK);
    if (AppComm::isTransferActive()) {
        text(ellipsize("Receiving " + AppComm::transferName(), w, FONT_SMALL), x, y + 112, FONT_SMALL, INK);
        progressBar(x, y + 152, w, 10, AppComm::transferPercent() / 100.0f);
    } else {
        std::string ev = AppComm::lastEvent();
        if (!ev.empty()) textWrapped("Last: " + ev, x, y + 112, w, FONT_SMALL, INK, 2);
    }
}

void SettingsApp::drawContent() {
    SystemSettingsData& s = SystemSettings::get();
    segmented(kTabs, {"Display", "Power", "Bluetooth", "About"}, m_tab);

    switch (m_tab) {
    case TAB_DISPLAY: {
        int y = CONTENT_Y;
        settingLabel(y, ROW, "Dark mode", "White text on black");
        Rect t = darkToggle();
        toggle(t.x, t.y, DisplayHAL::isDarkMode());
        hRule(y + ROW - 1);
        y += ROW;
        fontSizeRow(y);
        hRule(y + ROW - 1);
        y += ROW;
        settingLabel(y, 72, "Picture brightness", "Lightens photos and sleep pictures");
        segmented(brightnessSeg(), {"Original", "Light", "Lighter", "Lightest"}, s.imageBrightness, FONT_CAPTION + 1);
        break;
    }
    case TAB_POWER: {
        settingLabel(CONTENT_Y, 72, "Auto-sleep", "After this long without a touch");
        segmented(sleepSeg(), {"1m", "2m", "5m", "10m", "30m", "Never"}, sleepOptionIndex(s.sleepTimeoutMin));
        hRule(POWER_ROW2 - 1);
        settingLabel(POWER_ROW2, ROW, "Stay awake on USB", "While plugged into a computer");
        Rect u = usbToggle();
        toggle(u.x, u.y, s.stayAwakeOnUsb);
        hRule(POWER_ROW2 + ROW - 1);
        settingLabel(POWER_ROW2 + ROW, ROW, "Always on", "Developer: never auto-sleep");
        Rect a = alwaysToggle();
        toggle(a.x, a.y, s.alwaysOn);
        hRule(POWER_ROW4 - 1);
        text("Sleep screen", MARGIN, POWER_ROW4 + 12, FONT_BODY, BLACK);
        segmented(sleepScreenSeg(), {"Pictures", "Book & battery"}, s.sleepShowsImages ? 0 : 1);

        char batt[64];
        snprintf(batt, sizeof(batt), "%.2f V%s", DisplayHAL::getBatteryVoltage(),
                 PowerManager::isUsbConnected() ? "  |  USB connected" : "");
        settingLabel(POWER_BATT, 80, "Battery", batt);
        textRight(std::to_string(DisplayHAL::getBatteryPercent()) + "%", W - MARGIN, POWER_BATT + 24, FONT_BODY, BLACK, true);
        button(sleepNowBtn(), "Sleep now", ButtonStyle::PRIMARY, FONT_BODY);
        break;
    }
    case TAB_BLUETOOTH: {
        settingLabel(CONTENT_Y, ROW, "Bluetooth sync", "Companion app connection");
        Rect t = bleToggle();
        toggle(t.x, t.y, AppComm::isInitialized());
        drawBleStatusCard(BLE_CARD_Y);
        int y = BLE_CARD_Y + BLE_CARD_H + 36;
        sectionLabel(y, "How to connect");
        textWrapped("1. Open the companion app on your phone.\n2. Go to the Device tab and tap Connect.\n"
                    "3. Use the Library tab to browse, send or delete books.",
                    MARGIN, y + 36, W - 2 * MARGIN, FONT_SMALL, INK, 0, 10);
        break;
    }
    case TAB_ABOUT: {
        int y = CONTENT_Y;
        auto row = [&](const std::string& label, const std::string& value) {
            text(label, MARGIN, y + 22, FONT_BODY, BLACK);
            textRight(value, W - MARGIN, y + 24, FONT_SMALL + 2, INK, true);
            hRule(y + 75);
            y += 76;
        };
        row("Firmware", "v3.0.0");
#ifndef NATIVE_TESTING
        char buf[48];
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
        row("Free memory", buf);
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getFreePsram() / 1024));
        row("Free PSRAM", buf);
        uint64_t total = SD.totalBytes(), used = SD.usedBytes();
        snprintf(buf, sizeof(buf), "%.1f of %.1f GB free", (total - used) / 1073741824.0, total / 1073741824.0);
        row("SD card", buf);
        snprintf(buf, sizeof(buf), "%u min", (unsigned)(millis() / 60000));
        row("Awake for", buf);
#else
        row("Platform", "Desktop simulator");
#endif
        row("Display", "4.7\" 540 x 960, 16 greys");
        sectionLabel(y + 24, "Tips");
        textWrapped("Press the side button to sleep or wake. Waking returns you to the same page. "
                    "Tap the top of a book page for text size and progress.",
                    MARGIN, y + 60, W - 2 * MARGIN, FONT_SMALL, INK, 0, 8);
        break;
    }
    default:
        break;
    }
}

void SettingsApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header("Settings", true);
        drawContent();
    });
    m_lastBleStatus = bleStatusText();
}

void SettingsApp::redrawContent() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
        drawContent();
    });
    m_lastBleStatus = bleStatusText();
}

void SettingsApp::update() {
    if (m_tab != TAB_BLUETOOTH) return;
    uint32_t now = millis();
    if (now - m_lastStatusCheck < 1000) return;
    m_lastStatusCheck = now;
    std::string status = bleStatusText();
    if (status == m_lastBleStatus) return;
    m_lastBleStatus = status;
    UIFramework::performFastPartialUpdate(framebuffer, 0, BLE_CARD_Y - 4, W, BLE_CARD_H + 8, [this]() {
        drawBleStatusCard(BLE_CARD_Y);
    });
    // The status strip shows the link state too.
    UIFramework::performFastPartialUpdate(framebuffer, 0, 0, W, STATUS_H, []() { statusBar(); });
}

void SettingsApp::handleTouch(int x, int y) {
    SystemSettingsData& s = SystemSettings::get();

    int tab = hitSegmented(kTabs, TAB_COUNT, x, y);
    if (tab >= 0) {
        if (tab != m_tab) {
            m_tab = (Tab)tab;
            redrawContent();
        }
        return;
    }

    bool changed = false;
    switch (m_tab) {
    case TAB_DISPLAY: {
        if (y >= CONTENT_Y && y < CONTENT_Y + ROW && x > W / 2) {
            s.darkMode = !s.darkMode;
            DisplayHAL::setDarkMode(s.darkMode);
            SystemSettings::save();
            draw(); // whole screen inverts
            return;
        }
        float size = EReaderApp::getReadingFontSize();
        float next = size;
        if (fontMinus().contains(x, y) && size > 16.0f) next = size - 2.0f;
        if (fontPlus().contains(x, y) && size < 48.0f) next = size + 2.0f;
        if (next != size) {
            EReaderApp::setReadingFontSize(next);
            s.readingFontSize = next;
            changed = true;
        }
        int b = hitSegmented(brightnessSeg(), 4, x, y);
        if (b >= 0 && b != s.imageBrightness) {
            s.imageBrightness = b;
            changed = true;
        }
        break;
    }
    case TAB_POWER: {
        int i = hitSegmented(sleepSeg(), kSleepOptionCount, x, y);
        if (i >= 0 && kSleepOptions[i] != s.sleepTimeoutMin) {
            s.sleepTimeoutMin = kSleepOptions[i];
            changed = true;
        }
        if (y >= POWER_ROW2 && y < POWER_ROW2 + ROW && x > W / 2) {
            s.stayAwakeOnUsb = !s.stayAwakeOnUsb;
            changed = true;
        }
        if (y >= POWER_ROW2 + ROW && y < POWER_ROW4 && x > W / 2) {
            s.alwaysOn = !s.alwaysOn;
            changed = true;
        }
        int ss = hitSegmented(sleepScreenSeg(), 2, x, y);
        if (ss >= 0 && (ss == 0) != s.sleepShowsImages) {
            s.sleepShowsImages = (ss == 0);
            changed = true;
        }
        if (sleepNowBtn().contains(x, y)) {
            requestSleepNow();
            return;
        }
        break;
    }
    case TAB_BLUETOOTH: {
        if (y >= CONTENT_Y && y < CONTENT_Y + ROW && x > W / 2) {
            if (AppComm::isInitialized()) AppComm::deinit();
            else AppComm::init();
            s.bleEnabled = AppComm::isInitialized();
            changed = true;
        }
        break;
    }
    default:
        break;
    }

    if (changed) {
        SystemSettings::save();
        PowerManager::noteUserActivity();
        redrawContent();
    }
}

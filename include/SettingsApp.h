#pragma once

#include "Application.h"
#include <string>

enum SettingsPage {
    PAGE_MAIN,
    PAGE_BLE,
    PAGE_EREADER
};

class SettingsApp : public Application {
public:
    SettingsApp() = default;
    virtual ~SettingsApp() = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;

private:
    SettingsPage m_page = PAGE_MAIN;
    bool m_bleActive = false;
    std::string m_syncStatus = "";
    bool m_processingTouch = false;
    bool m_lastDrawnBleActive = false;
    std::string m_lastDrawnStatus = "";
    bool m_hasDrawn = false;
    SettingsPage m_lastDrawnPage = PAGE_MAIN;

    void drawMainSettings();
    void drawBleSettings(bool forceFullRefresh);
    void drawEReaderSettings();
    void drawLogLineOnly();
};

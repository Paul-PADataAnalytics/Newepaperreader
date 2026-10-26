#pragma once

#include "Application.h"
#include <string>

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
    enum Tab { TAB_DISPLAY = 0, TAB_POWER, TAB_BLUETOOTH, TAB_ABOUT, TAB_COUNT };

    Tab m_tab = TAB_DISPLAY;
    std::string m_lastBleStatus;
    uint32_t m_lastStatusCheck = 0;

    void drawContent();
    void redrawContent();
    std::string bleStatusText() const;
    void drawBleStatusCard(int y);
};

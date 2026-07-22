#pragma once

#include "Application.h"
#include <vector>
#include <string>

struct AppDescriptor {
    std::string name;
    std::string description;
    Application* (*factory)();
};

class Launcher {
public:
    static Launcher& getInstance();

    void init();
    void loop();

    void launchApp(int index);
    void switchToApp(int index);
    void exitCurrentApp();

    void drawMenu();
    void handleTouch(int x, int y);

    bool isAppRunning() const { return m_activeApp != nullptr; }
    Application* getActiveApp() { return m_activeApp; }

private:
    Launcher() = default;
    ~Launcher();
    Launcher(const Launcher&) = delete;
    Launcher& operator=(const Launcher&) = delete;

    std::vector<AppDescriptor> m_apps;
    Application* m_activeApp = nullptr;
    int m_activeAppIndex = -1;
};

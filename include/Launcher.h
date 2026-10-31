#pragma once

#include "Application.h"
#include "ui/ui_icons.h"
#include <vector>
#include <string>

// Stable app identifiers (also persisted in the sleep/resume breadcrumb).
enum AppId {
    APP_READER = 0,
    APP_BOOKMARKS,
    APP_IMAGES,
    APP_FILES,
    APP_CALCULATOR,
    APP_SETTINGS,
    APP_TEARACE,
    APP_KEEPER,
    APP_GAMES,
    APP_CODEBREAKER,
    APP_COUNT
};

struct AppDescriptor {
    std::string name;
    std::string description;
    Application* (*factory)();
    UIIconId icon;
    AppDescriptor(const std::string& n = "", const std::string& d = "", Application* (*f)() = nullptr, UIIconId i = ICON_BOOK)
        : name(n), description(d), factory(f), icon(i) {}
};

class Launcher {
public:
    static Launcher& getInstance();

    void init();
    void loop();

    // Immediate launch (only safe outside an app's own event handlers).
    void launchApp(int id, bool autoDraw = true);
    void switchToApp(int id) { launchApp(id); }
    void exitCurrentApp();

    // Deferred variants: safe to call from inside an app's handleTouch() /
    // update(); applied once that call has returned.
    // With returnTo set, closing the launched app goes back to that app
    // instead of the home screen.
    void requestLaunch(int id, int returnTo = -1);
    void requestExit();
    // Opens the e-reader straight into the given book.
    void requestOpenBook(const std::string& runtimePath);

    void drawMenu();
    void handleTouch(int x, int y);
    // When loop() next has time-based work (the active app's update(), or
    // the home-screen clock's minute tick); see Application::msUntilUpdate().
    uint32_t msUntilUpdate() const;

    bool isAppRunning() const { return m_activeApp != nullptr; }
    Application* getActiveApp() { return m_activeApp; }
    int getActiveAppIndex() const { return m_activeAppIndex; }

private:
    Launcher() = default;
    ~Launcher();
    Launcher(const Launcher&) = delete;
    Launcher& operator=(const Launcher&) = delete;

    void processPending();
    void drawClock();

    std::vector<AppDescriptor> m_apps;
    Application* m_activeApp = nullptr;
    int m_activeAppIndex = -1;

    int m_pendingLaunch = -1;
    int m_pendingReturnTo = -1;
    int m_returnTo = -1;
    bool m_pendingExit = false;
    std::string m_pendingBook;

    std::string m_lastBookPath;
};

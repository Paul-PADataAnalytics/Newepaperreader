#pragma once

#include <vector>
#include <string>

struct NavTarget {
    int appIndex;      // App index (-1 = Launcher main menu)
    int internalState; // App-specific sub-state ID (e.g. STATE_LIB=0, STATE_READ=1)
    bool isPortrait;   // Screen orientation
    std::string path;  // Path or context parameter

    NavTarget(int appIdx = -1, int state = 0, bool portrait = false, const std::string& p = "")
        : appIndex(appIdx), internalState(state), isPortrait(portrait), path(p) {}
};

class NavigationManager {
public:
    static NavigationManager& getInstance();

    // Push a new target onto the back stack and activate it
    void navigateTo(const NavTarget& target);

    // Pop the current top view and return to the previous view
    bool goBack();

    // Clear history stack (e.g. returning to home launcher)
    void clearHistory();

    // Get current top target
    NavTarget getCurrentTarget() const;

    // Check if back navigation is available
    bool canGoBack() const;

private:
    NavigationManager() = default;
    ~NavigationManager() = default;
    NavigationManager(const NavigationManager&) = delete;
    NavigationManager& operator=(const NavigationManager&) = delete;

    std::vector<NavTarget> m_stack;
};

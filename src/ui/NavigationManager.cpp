#include "NavigationManager.h"
#include "Launcher.h"
#include "DisplayHAL.h"
#include <stdio.h>

NavigationManager& NavigationManager::getInstance() {
    static NavigationManager instance;
    return instance;
}

void NavigationManager::navigateTo(const NavTarget& target) {
    // Avoid pushing duplicate identical state on top
    if (!m_stack.empty()) {
        const auto& cur = m_stack.back();
        if (cur.appIndex == target.appIndex && 
            cur.internalState == target.internalState && 
            cur.path == target.path &&
            cur.isPortrait == target.isPortrait) {
            return;
        }
    }
    m_stack.push_back(target);
}

bool NavigationManager::goBack() {
    if (m_stack.size() <= 1) {
        // At or below root: reset to main Launcher menu
        m_stack.clear();
        NavTarget home{-1, 0, false, ""};
        m_stack.push_back(home);

        DisplayHAL::setPortrait(false);
        Launcher::getInstance().exitCurrentApp();
        return false;
    }

    // Pop active top state
    m_stack.pop_back();

    // Target to restore
    NavTarget prev = m_stack.back();

    DisplayHAL::setPortrait(prev.isPortrait);

    if (prev.appIndex < 0) {
        Launcher::getInstance().exitCurrentApp();
    } else {
        Launcher::getInstance().switchToApp(prev.appIndex);
    }
    return true;
}

void NavigationManager::clearHistory() {
    m_stack.clear();
}

NavTarget NavigationManager::getCurrentTarget() const {
    if (m_stack.empty()) {
        return NavTarget{-1, 0, false, ""};
    }
    return m_stack.back();
}

bool NavigationManager::canGoBack() const {
    return m_stack.size() > 1;
}

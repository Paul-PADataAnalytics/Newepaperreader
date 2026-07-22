#pragma once

#include "Application.h"
#include "EBookmarkManager.h"
#include "ui/AppScreens.h"
#include <vector>

class EBookmarkApp : public Application {
public:
    EBookmarkApp() = default;
    virtual ~EBookmarkApp() = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;

private:
    enum BookmarkState {
        STATE_LIST,
        STATE_DETAIL
    };

    BookmarkState m_state = STATE_LIST;
    std::vector<EBookmark> m_sortedEBookmarks;
    int m_selectedBookmarkIndex = -1;
    int m_ebookmarkPage = 0;
    LibrarySort m_librarySort = LibrarySort::AUTHOR;

    // Last known database state for automatic two-way sync.
    std::string m_lastSyncedJson;
    uint32_t m_lastLocalUpdateTs = 0;

    void updateEBookmarkItems();
    void drawEBookmarkLibrary();
    void drawEBookmarkDetail(bool fullRefresh = false);
    void syncFromApp();
    void syncToApp();
};

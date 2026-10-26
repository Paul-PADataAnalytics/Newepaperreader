#pragma once

#include "Application.h"
#include "EBookmarkManager.h"
#include <string>
#include <vector>

// "Bookmarks" app: physical books tracked with the phone companion app.
// List of books (sortable) -> detail view with progress, stats, history and
// quick page adjust controls.
class EBookmarkApp : public Application {
public:
    EBookmarkApp() = default;
    virtual ~EBookmarkApp() = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;
    virtual bool onBack() override;

private:
    enum BookmarkState {
        STATE_LIST,
        STATE_DETAIL
    };

    BookmarkState m_state = STATE_LIST;
    std::vector<EBookmark> m_sortedEBookmarks;
    std::string m_selectedIsbn;
    int m_ebookmarkPage = 0;
    LibrarySort m_librarySort = LibrarySort::AUTHOR;

    uint32_t m_seenRevision = 0;
    int m_detailBodyTop = 0; // y where the page-dependent part of the detail view starts

    // Quick page adjustments are shown immediately but committed to the
    // manager (history entry + save + phone delta) only once the user stops
    // tapping, so a burst of +1 taps becomes a single reading "sitting".
    int m_pendingPage = -1;
    uint32_t m_pendingSinceMs = 0;

    void updateEBookmarkItems();
    int listPageCount() const;
    const EBookmark* selectedBook() const;
    int displayedPage(const EBookmark& book) const;

    void drawEBookmarkLibrary(bool fullRefresh = true);
    void drawListContent();
    void drawEBookmarkDetail(bool fullRefresh = true);
    int drawDetailTitleBlock(const EBookmark& book);
    void drawDetailBody(const EBookmark& book, int top);

    void handleListTouch(int x, int y);
    void handleDetailTouch(int x, int y);
    void adjustPage(int delta);
    bool commitPendingPage();
};

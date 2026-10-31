#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include "reader/TextReader.h"
#include "EpubParser.h"
#include "ui/AppScreens.h"
#include <vector>
#include <string>
#include <cstdint>

class EReaderApp : public Application {
public:
    EReaderApp() = default;
    virtual ~EReaderApp() = default;

    static void setReadingFontSize(float pt);
    static float getReadingFontSize();

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual bool wantsSystemBack() const override { return m_state == STATE_LIB; }

    // True while a book page is actively displayed (STATE_READ), as opposed
    // to the library/file list view. Used by main.cpp to exempt active
    // reading from the 30s inactivity auto-lock timeout.
    bool isReadingBook() const { return m_state == STATE_READ; }

    // Fully-qualified runtime path of the book currently open for reading
    // (empty if none). Used by main.cpp to save a resume breadcrumb before
    // entering deep-sleep lock mode.
    const std::string& getCurrentBookPath() const { return m_currentBookPath; }
    const std::string& getCurrentBookTitle() const { return m_currentBookTitle; }

    // Jumps directly into reading the book at the given runtime path,
    // skipping the library view - used to resume reading after a
    // deep-sleep wake, when the library may not have been scanned yet this
    // boot. Title/author are derived from the filename (same fallback
    // openBook() uses when a book isn't found in parsed library metadata).
    void resumeAtPath(const std::string& runtimePath);

    // Persists the exact current reading position to this book's bookmark
    // file immediately - called before entering deep-sleep lock mode so a
    // subsequent resume (which reloads via the saved bookmark) lands on the
    // precise page being read, regardless of how long ago the last
    // forward/back page turn happened to save one. No-op if not currently
    // reading a book.
    void saveCurrentPosition();

    // Persists the position and the home screen's "continue reading" info.
    void prepareForSleep() { saveCurrentPosition(); rememberLastBook(); }

private:
    enum ReaderState {
        STATE_LIB,
        STATE_READ
    };

    struct LibraryItem {
        FileInfo file;
        std::string title;
        std::string author;
        int completionPercent;
    };

    ReaderState m_state = STATE_LIB;
    std::vector<FileInfo> m_libraryFiles;
    std::vector<LibraryItem> m_parsedLibraryItems;
    char* m_currentBookText = nullptr;
    size_t m_currentBookTextLen = 0;
    int m_currentReadingOffset = 0;
    std::vector<size_t> m_pageHistory;
    std::string m_currentBookPath = "";
    std::string m_currentBookTitle = "";
    std::string m_currentBookAuthor = "";
    int m_libraryPage = 0;
    static LibrarySort s_librarySort;
    bool m_menuOpen = false;

    std::vector<std::string> m_chapters;
    int m_currentChapterIndex = -1;
    bool loadChapter(int index);
    bool m_chapterPopup = false; // the Contents screen is showing
    int m_chapterPage = 0;
    std::string m_chapterError;

    // Table of contents of the open book, built on first use and cached
    // until another book is opened (see EReaderContents.cpp). EPUB entries
    // point at a spine item plus an offset into its stripped text; text-book
    // entries point at a byte offset in the file (chapter is always 0).
    struct ContentsEntry {
        std::string title;
        std::string fragment; // EPUB anchor still to be resolved to an offset
        int chapter = 0;
        int offset = 0;       // -1 = EPUB anchor not resolved yet
        int depth = 0;
    };
    std::vector<ContentsEntry> m_contents;
    bool m_contentsLoaded = false;
    // EPUB with no usable TOC: one entry per spine item, titled lazily.
    bool m_contentsFromSpine = false;
    int m_gotoPercent = 0;

    void ensureContents();
    void buildTextContents();
    const std::string& contentsTitle(int index);
    std::string epubHeadingTitle(int chapter);
    void resolveContentsAnchors(int chapter, const char* html, size_t htmlLen,
                                const char* text, size_t textLen);
    void resolveCurrentChapterAnchors();
    uint64_t contentsKey(int index) const;
    uint64_t currentPositionKey() const;
    // Entry containing the current page (-1 if before the first entry).
    int currentContentsEntry();
    // Previous/next chapter target from the current page, or -1. Text
    // books whose headings haven't been scanned yet report -2 (unknown).
    int adjacentContentsEntry(bool forward);
    bool goToAdjacentChapter(bool forward);
    bool jumpToContentsEntry(int index);
    bool jumpToTextPosition(size_t pos);
    bool jumpToPercent(int percent);
    void finishJump();
    void loadTextChunk();
    std::string currentChapterLabel();
    void openContents();
    void drawContents();
    void drawGotoPanel();
    void handleContentsTouch(int x, int y);
    int contentsListTop() const;
    int contentsRowsPerPage() const;
    static std::string htmlToText(const char* html, size_t len);

    FileBrowser m_fileBrowser;
    EpubParser m_epubParser;
    TextReader m_textReader;

    static float s_readingFontSize;

    void prepareTypographyForReading();
    void restoreTypographyDefaults();
    // Whole-book progress 0..1 (chapter-aware for EPUB).
    float currentProgress() const;
    void rememberLastBook();
    void leaveReading();
    void turnPage(bool forward);
    void drawReadingMenu();
    int libraryRowsPerPage() const;
    void updateLibraryItems();
    // Full recursive SD scan (slow) - only called when a cached index isn't
    // usable (see AppStorage::loadLibraryIndex). Populates m_libraryFiles and
    // persists the result so most future launches can skip this entirely.
    void rescanLibraryFromDisk();
    // Rebuilds m_parsedLibraryItems (titles/authors/progress %) from the
    // already-known m_libraryFiles and re-sorts - no disk directory walk, so
    // it's cheap enough to call on every library redraw (sort/page change,
    // returning from reading, etc.).
    void rebuildParsedLibraryItems();
    void sortLibraryItems();
    void drawLibrary();
    // quick = flash-free page-turn refresh (falls back to a full refresh
    // every SystemSettings::fullRefreshEvery pages).
    void drawReading(bool quick = false);
    int m_turnsSinceFullRefresh = 0;
    void openBook(int index);
    // Shared tail of openBook()/resumeAtPath(): loads the book at `path`
    // (m_currentBookTitle/m_currentBookAuthor must already be set by the
    // caller), restores its saved bookmark offset, and enters STATE_READ.
    void openBookAtRuntimePath(const std::string& path);
    void drawScanningProgressScreen(int dirsVisited);
    void updateScanProgressCount(int dirsVisited);
    // Immediate "please wait" feedback shown the instant the app is entered,
    // before any SD card work (font load skip-check, cache load, per-book
    // bookmark reads) has a chance to run - see onCreate(). Uses a fast
    // partial update, not a full-screen draw, so it never adds a second
    // slow e-ink refresh cycle on top of the one drawLibrary() already does.
    void drawOpeningLibraryScreen();
};

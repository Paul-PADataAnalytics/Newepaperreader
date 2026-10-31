#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include "reader/TextReader.h"
#include "EpubParser.h"
#include "reader/LibraryCatalog.h"
#include "ui/AppScreens.h"
#include <unordered_map>
#include <vector>
#include <string>

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
    // Collects finished background cover decodes and draws them in place.
    virtual void update() override;
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
        std::string coverId;
        uint8_t coverState; // BookCover::State
    };

    ReaderState m_state = STATE_LIB;
    std::vector<FileInfo> m_libraryFiles;
    std::vector<LibraryItem> m_parsedLibraryItems;
    // Cached per-book details (see LibraryCatalog), parallel to m_libraryFiles.
    std::vector<LibraryCatalog::Record> m_records;
    bool m_catalogDirty = false;
    bool m_libraryListComplete = true; // false after a partial/failed walk
    bool m_coverWorkBusy = false;
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
    std::vector<std::string> m_chapterTitles;
    int m_currentChapterIndex = -1;
    bool loadChapter(int index);
    bool m_chapterPopup = false;
    int m_chapterPage = 0;
    std::string m_chapterError;

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
    // Full recursive SD scan - only called when a cached index isn't usable
    // (see AppStorage::loadLibraryIndex). Populates m_libraryFiles, collects
    // the .bmk bookmark files it saw, and persists the book list so most
    // future launches can skip the walk entirely.
    void rescanLibraryFromDisk(std::unordered_map<std::string, FileInfo>& bookmarkFiles);
    // Brings m_records up to date for m_libraryFiles (parsing only new or
    // changed EPUBs, reading only changed bookmarks), then rebuilds the items.
    void syncLibraryCatalog(const std::unordered_map<std::string, FileInfo>* bookmarkFiles,
                            const std::string& refreshBookmarkFor);
    // Rebuilds m_parsedLibraryItems (titles/authors/progress %) from
    // m_records and re-sorts. Called after returning from a book: only that
    // book's bookmark is re-read.
    void rebuildParsedLibraryItems();
    void buildLibraryItems();
    // Cover thumbnail (or a placeholder) for one library row.
    void drawLibraryCover(const LibraryItem& item, int x, int y);
    // Queues background decodes for covers on the current page not yet cached.
    void requestVisibleCovers();
    // Ensures the open book's cover (used by the sleep screen) is cached.
    void requestCurrentBookCover();
    void sortLibraryItems();
    void drawLibrary();
    void drawChapterPopup();
    // quick = flash-free page-turn refresh (falls back to a full refresh
    // every SystemSettings::fullRefreshEvery pages).
    void drawReading(bool quick = false);
    int m_turnsSinceFullRefresh = 0;
    void openBook(int index);
    // Shared tail of openBook()/resumeAtPath(): loads the book at `path`
    // (m_currentBookTitle/m_currentBookAuthor must already be set by the
    // caller), restores its saved bookmark offset, and enters STATE_READ.
    void openBookAtRuntimePath(const std::string& path);
    // "Opening library..." card text, updated in place (fast partial refresh).
    void updateOpeningLibraryScreen(const std::string& detail);
    // Immediate "please wait" feedback shown the instant the app is entered,
    // before any SD card work (font load skip-check, cache load, per-book
    // bookmark reads) has a chance to run - see onCreate(). Uses a fast
    // partial update, not a full-screen draw, so it never adds a second
    // slow e-ink refresh cycle on top of the one drawLibrary() already does.
    void drawOpeningLibraryScreen();
};

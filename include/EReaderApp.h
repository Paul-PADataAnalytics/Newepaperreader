#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include "reader/TextReader.h"
#include "EpubParser.h"
#include "ui/AppScreens.h"
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

    // True while a book page is actively displayed (STATE_READ), as opposed
    // to the library/file list view. Used by main.cpp to exempt active
    // reading from the 30s inactivity auto-lock timeout.
    bool isReadingBook() const { return m_state == STATE_READ; }

    // Fully-qualified runtime path of the book currently open for reading
    // (empty if none). Used by main.cpp to save a resume breadcrumb before
    // entering deep-sleep lock mode.
    const std::string& getCurrentBookPath() const { return m_currentBookPath; }

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
    LibrarySort m_librarySort = LibrarySort::AUTHOR;

    std::vector<std::string> m_chapters;
    int m_currentChapterIndex = -1;
    void loadChapter(int index);

    FileBrowser m_fileBrowser;
    EpubParser m_epubParser;
    TextReader m_textReader;

    static float s_readingFontSize;

    void prepareTypographyForReading();
    void updateLibraryItems();
    void drawLibrary();
    void drawReading();
    void openBook(int index);
    // Shared tail of openBook()/resumeAtPath(): loads the book at `path`
    // (m_currentBookTitle/m_currentBookAuthor must already be set by the
    // caller), restores its saved bookmark offset, and enters STATE_READ.
    void openBookAtRuntimePath(const std::string& path);
    void drawScanningProgressScreen(int dirsVisited);
    void updateScanProgressCount(int dirsVisited);
};

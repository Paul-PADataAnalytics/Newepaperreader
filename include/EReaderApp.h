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
    std::string m_currentBookPath = "";
    std::string m_currentBookTitle = "";
    std::string m_currentBookAuthor = "";
    int m_libraryPage = 0;
    LibrarySort m_librarySort = LibrarySort::AUTHOR;

    FileBrowser m_fileBrowser;
    EpubParser m_epubParser;
    TextReader m_textReader;

    static float s_readingFontSize;

    void updateLibraryItems();
    void drawLibrary();
    void drawReading();
    void openBook(int index);
};

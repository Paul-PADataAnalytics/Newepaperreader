import re

with open('src/main.cpp', 'r') as f:
    content = f.read()

# 1. Add include
content = content.replace('#include "reader/FileBrowser.h"', '#include "reader/FileBrowser.h"\n#include "reader/TextReader.h"')

# 2. Add textReader instance
content = content.replace('EpubParser epubParser;', 'EpubParser epubParser;\nTextReader textReader;')

# 3. Modify openBook
openBook_orig = """void openBook(int index) {
    if (index < 0 || index >= libraryFiles.size()) return;

    std::string path = libraryFiles[index].path;
#ifndef NATIVE_TESTING
    path = "/sd" + path;
#endif

#ifndef NATIVE_TESTING
    Serial.printf("Opening book: %s\\n", path.c_str());
#endif

    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, "Loading...", 100);
    DisplayHAL::display(framebuffer);

    if (epubParser.open(path)) {
        auto chapters = epubParser.getChapterList();
        if (!chapters.empty()) {
            std::string html = epubParser.getFileContent(chapters[0]);
            currentBookText = stripHTML(html);
        } else {
            currentBookText = "No chapters found in EPUB.";
        }
        epubParser.close();
    } else {
        currentBookText = "Failed to parse EPUB.";
    }

    currentReadingOffset = 0;
    currentState = STATE_READING;
    drawReading();
}"""

openBook_new = """void openBook(int index) {
    if (index < 0 || index >= libraryFiles.size()) return;

    std::string path = libraryFiles[index].path;
#ifndef NATIVE_TESTING
    path = "/sd" + path;
#endif

#ifndef NATIVE_TESTING
    Serial.printf("Opening book: %s\\n", path.c_str());
#endif

    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, "Loading...", 100);
    DisplayHAL::display(framebuffer);

    textReader.closeFile(); // ensure closed
    bool isText = (path.length() >= 4 && path.substr(path.length() - 4) == ".txt") ||
                  (path.length() >= 3 && path.substr(path.length() - 3) == ".md");

    if (isText) {
        if (textReader.openFile(path.c_str())) {
            currentBookText = textReader.getPageText();
        } else {
            currentBookText = "Failed to open text file.";
        }
    } else {
        if (epubParser.open(path)) {
            auto chapters = epubParser.getChapterList();
            if (!chapters.empty()) {
                std::string html = epubParser.getFileContent(chapters[0]);
                currentBookText = stripHTML(html);
            } else {
                currentBookText = "No chapters found in EPUB.";
            }
            epubParser.close();
        } else {
            currentBookText = "Failed to parse EPUB.";
        }
    }

    currentReadingOffset = 0;
    currentState = STATE_READING;
    drawReading();
}"""
content = content.replace(openBook_orig, openBook_new)

# 4. Modify handleTouch
handleTouch_orig = """void handleTouch(int x, int y) {
    if (currentState == STATE_LIBRARY) {
        int index = (y - 60) / 60;
        if (index >= 0 && index < libraryFiles.size()) {
            openBook(index);
        }
    } else if (currentState == STATE_READING) {
        if (y < 60 && x < 100) {
            currentState = STATE_LIBRARY;
            drawLibrary();
        } else if (x > 960 / 2) {
            currentReadingOffset += 1200; // rough guess
            if (currentReadingOffset > currentBookText.length()) currentReadingOffset = currentBookText.length();
            drawReading();
        } else {
            currentReadingOffset -= 1200;
            if (currentReadingOffset < 0) currentReadingOffset = 0;
            drawReading();
        }
    }
}"""

handleTouch_new = """void handleTouch(int x, int y) {
    if (currentState == STATE_LIBRARY) {
        int index = (y - 60) / 60;
        if (index >= 0 && index < libraryFiles.size()) {
            openBook(index);
        }
    } else if (currentState == STATE_READING) {
        if (y < 60 && x < 100) {
            if (textReader.isOpen()) textReader.closeFile();
            currentState = STATE_LIBRARY;
            drawLibrary();
        } else if (x > 960 / 2) {
            if (textReader.isOpen()) {
                textReader.nextPage();
                currentBookText = textReader.getPageText();
                currentReadingOffset = 0;
            } else {
                currentReadingOffset += 1200; // rough guess
                if (currentReadingOffset > currentBookText.length()) currentReadingOffset = currentBookText.length();
            }
            drawReading();
        } else {
            if (textReader.isOpen()) {
                textReader.prevPage();
                currentBookText = textReader.getPageText();
                currentReadingOffset = 0;
            } else {
                currentReadingOffset -= 1200;
                if (currentReadingOffset < 0) currentReadingOffset = 0;
            }
            drawReading();
        }
    }
}"""
content = content.replace(handleTouch_orig, handleTouch_new)

with open('src/main.cpp', 'w') as f:
    f.write(content)

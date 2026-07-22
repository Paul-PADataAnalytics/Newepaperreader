#include <cstring>

#include "TextReader.h"
#include "MarkdownParser.h"
#include "RTFParser.h"

TextReader::TextReader() : fileSize(0), currentPosition(0) {
    memset(pageBuffer, 0, sizeof(pageBuffer));
}

TextReader::~TextReader() {
    closeFile();
}

bool TextReader::openFile(const char* filepath) {
    closeFile();

#ifndef NATIVE_TESTING
    file = SD.open(filepath, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open text file: %s\n", filepath);
        return false;
    }
    fileSize = file.size();
#else
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        printf("Failed to open text file natively: %s\n", filepath);
        return false;
    }
    file = (void*)f;
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif

    currentFilePath = filepath;
    currentPosition = 0;

    pageHistory.clear();
    pageHistory.push_back(0); // Page 0 starts at byte 0

    return true;
}

void TextReader::closeFile() {
#ifndef NATIVE_TESTING
    if (file) {
        file.close();
    }
#else
    if (file) {
        fclose((FILE*)file);
        file = nullptr;
    }
#endif
    currentFilePath = "";
    fileSize = 0;
    currentPosition = 0;
}

std::string TextReader::getPageText() {
    if (!file) return "";

#ifndef NATIVE_TESTING
    file.seek(currentPosition);

    // Read a chunk of text. In a real scenario, this would read until the
    // typography engine says the screen is full. For basic implementation,
    // we just read a fixed amount of bytes.
    size_t bytesRead = file.read((uint8_t*)pageBuffer, sizeof(pageBuffer) - 1);
#else
    fseek((FILE*)file, currentPosition, SEEK_SET);
    size_t bytesRead = fread(pageBuffer, 1, sizeof(pageBuffer) - 1, (FILE*)file);
#endif
    pageBuffer[bytesRead] = '\0'; // Null-terminate

    std::string text(pageBuffer);
    
    // Process markdown if the file has .md extension
    if (currentFilePath.length() >= 3 && 
        currentFilePath.substr(currentFilePath.length() - 3) == ".md") {
        text = MarkdownParser::stripMarkdown(text);
    } else if (currentFilePath.length() >= 4 && 
               (currentFilePath.substr(currentFilePath.length() - 4) == ".rtf" || 
                currentFilePath.substr(currentFilePath.length() - 4) == ".RTF")) {
        text = RTFParser::stripRTF(text);
    }

    return text;
}

void TextReader::nextPage() {
    if (!file) return;

    // Real logic: Advance currentPosition by the exact number of bytes that fit on the screen
    // Placeholder: Advance by size of our buffer
    size_t bytesToAdvance = sizeof(pageBuffer) - 1;

    if (currentPosition + bytesToAdvance < fileSize) {
        currentPosition += bytesToAdvance;
        pageHistory.push_back(currentPosition);
    }
}

void TextReader::prevPage() {
    if (!file || pageHistory.size() <= 1) return;

    // Pop current page
    pageHistory.pop_back();
    // Get start position of the new current page
    currentPosition = pageHistory.back();
}

float TextReader::getProgress() const {
    if (fileSize == 0) return 0.0f;
    return (float)currentPosition / (float)fileSize;
}

bool TextReader::isOpen() const {
#ifndef NATIVE_TESTING
    return file == true;
#else
    return file != nullptr;
#endif
}

size_t TextReader::getPosition() const {
    return currentPosition;
}

void TextReader::setPosition(size_t pos) {
    if (pos < fileSize) {
        currentPosition = pos;
        // Reset page history to avoid weird back-tracking
        pageHistory.clear();
        pageHistory.push_back(0); // Root
        if (pos > 0) pageHistory.push_back(pos);
    }
}

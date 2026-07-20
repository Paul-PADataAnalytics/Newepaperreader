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

    file = SD.open(filepath, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open text file: %s\n", filepath);
        return false;
    }

    currentFilePath = filepath;
    fileSize = file.size();
    currentPosition = 0;

    pageHistory.clear();
    pageHistory.push_back(0); // Page 0 starts at byte 0

    return true;
}

void TextReader::closeFile() {
    if (file) {
        file.close();
    }
    currentFilePath = "";
    fileSize = 0;
    currentPosition = 0;
}

std::string TextReader::getPageText() {
    if (!file) return "";

    file.seek(currentPosition);

    // Read a chunk of text. In a real scenario, this would read until the
    // typography engine says the screen is full. For basic implementation,
    // we just read a fixed amount of bytes.
    size_t bytesRead = file.read((uint8_t*)pageBuffer, sizeof(pageBuffer) - 1);
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
    return file == true;
}

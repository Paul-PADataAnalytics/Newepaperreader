#pragma once

#include <Arduino.h>
#include <SD.h>
#include <string>
#include <vector>

class TextReader {
public:
    TextReader();
    ~TextReader();

    // Opens a text file for reading
    bool openFile(const char* filepath);
    
    // Closes the currently open file
    void closeFile();
    
    // Reads a chunk of text to fill the screen (basic pagination)
    // Returns the text for the current page
    std::string getPageText();
    
    // Navigates pages
    void nextPage();
    void prevPage();
    
    // Gets the current progress (0.0 to 1.0)
    float getProgress() const;
    
    // Check if a file is open
    bool isOpen() const;

private:
    File file;
    std::string currentFilePath;
    
    // Pagination state
    size_t fileSize;
    size_t currentPosition;
    
    // Buffer for reading page text
    char pageBuffer[2048]; // Approximate chars that fit on screen, adjust as needed
    
    // We would need to calculate where the page actually ends based on font rendering
    // For now, this stores the start byte offset of previous pages to allow going backward
    std::vector<size_t> pageHistory;
};

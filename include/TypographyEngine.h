#pragma once
#include <stdint.h>
#include <string>

class TypographyEngine {
public:
    TypographyEngine();
    ~TypographyEngine();

    // Load font from a TTF file (from SD card or native filesystem)
    bool loadFont(const char* filepath, float fontSize);
    
    bool loadFontFromMemory(const uint8_t* fontData, size_t size, float fontSize);
    
    void setFontSize(float size);

    // Render a paragraph starting at given x, y with fixed margins.
    // Text layout uses Glyph-Exact Pagination Algorithm.
    // forward layout from top-left, reverse layout from bottom-right (for the chunk, we just need forward rendering).
    // Margins should be globally fixed variables.
    void renderText(const std::string& text, int startX, int startY, uint8_t* framebuffer, uint8_t textColor = 0);
    void renderText(const char* text, size_t len, int startX, int startY, uint8_t* framebuffer, uint8_t textColor = 0);

    // Reverse layout: finds the start index of the page preceding the given current index.
    // Lays out text backwards from bottom-right to top-left to determine how many glyphs fit.
    size_t findPreviousPageStart(const std::string& text, size_t currentIndex);
    size_t findPreviousPageStart(const char* text, size_t len, size_t currentIndex);

    // Forward layout: finds the exact start index of the NEXT page given the current index.
    size_t findNextPageStart(const std::string& text, size_t currentIndex);
    size_t findNextPageStart(const char* text, size_t len, size_t currentIndex);

    // Measure the width of a single line of text in pixels
    int measureText(const std::string& text);
    int measureText(const char* text, size_t len);

    // Override margins at runtime (used to reserve space for UI elements)
    void setTopMargin(int pixels);
    void setBottomMargin(int pixels);

private:
    uint8_t* fontBuffer; // PSRAM memory for TTF file
    bool isEmbeddedFont;
    float fontSize;
    void* fontInfo; // Pointer to stbtt_fontinfo
    
    // Globally fixed margins
    int       MARGIN_TOP    = 20;   // mutable — can be overridden at runtime
    int       MARGIN_BOTTOM = 20;   // mutable — can be overridden at runtime
    const int MARGIN_LEFT   = 20;
    const int MARGIN_RIGHT  = 20;
    const int LINE_SPACING  = 5;
};

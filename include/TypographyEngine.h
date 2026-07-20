#pragma once
#include <stdint.h>
#include <string>

class TypographyEngine {
public:
    TypographyEngine();
    ~TypographyEngine();

    // Load font from a TTF file (from SD card or native filesystem)
    bool loadFont(const char* filepath, float fontSize);
    
    // Load font from embedded memory array
    bool loadFontFromMemory(const uint8_t* fontData, size_t size, float fontSize);

    // Render a paragraph starting at given x, y with fixed margins.
    // Text layout uses Glyph-Exact Pagination Algorithm.
    // forward layout from top-left, reverse layout from bottom-right (for the chunk, we just need forward rendering).
    // Margins should be globally fixed variables.
    void renderText(const std::string& text, int startX, int startY, uint8_t* framebuffer);

    // Reverse layout: finds the start index of the page preceding the given current index.
    // Lays out text backwards from bottom-right to top-left to determine how many glyphs fit.
    size_t findPreviousPageStart(const std::string& text, size_t currentIndex);

private:
    uint8_t* fontBuffer; // PSRAM memory for TTF file
    bool isEmbeddedFont;
    float fontSize;
    void* fontInfo; // Pointer to stbtt_fontinfo
    
    // Globally fixed margins
    const int MARGIN_TOP = 20;
    const int MARGIN_BOTTOM = 20;
    const int MARGIN_LEFT = 20;
    const int MARGIN_RIGHT = 20;
    const int LINE_SPACING = 5;
};

#include "TypographyEngine.h"
#include "DisplayHAL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// If not native, heap_caps_malloc is needed.
#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif

static void drawPixel(int x, int y, uint8_t color, uint8_t* framebuffer) {
    DisplayHAL::setPixel(x, y, color, framebuffer);
}

static uint8_t getPixel(int x, int y, uint8_t* framebuffer) {
    return DisplayHAL::getPixel(x, y, framebuffer);
}

TypographyEngine::TypographyEngine() : fontBuffer(nullptr), isEmbeddedFont(false), fontSize(16.0f), fontInfo(nullptr) {
    fontInfo = malloc(sizeof(stbtt_fontinfo));
}

TypographyEngine::~TypographyEngine() {
    if (fontBuffer && !isEmbeddedFont) {
#ifndef NATIVE_TESTING
        heap_caps_free(fontBuffer);
#else
        free(fontBuffer);
#endif
    }
    if (fontInfo) {
        free(fontInfo);
    }
}

bool TypographyEngine::loadFont(const char* filepath, float size) {
    this->fontSize = size;
    
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        printf("Failed to open font file: %s\n", filepath);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
#ifndef NATIVE_TESTING
    // Allocate in PSRAM
    fontBuffer = (uint8_t*)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM);
#else
    fontBuffer = (uint8_t*)malloc(fsize);
#endif

    if (!fontBuffer) {
        printf("Failed to allocate memory for font\n");
        fclose(f);
        return false;
    }
    
    fread(fontBuffer, 1, fsize, f);
    fclose(f);
    
    if (!stbtt_InitFont((stbtt_fontinfo*)fontInfo, fontBuffer, 0)) {
        printf("Failed to init font\n");
        return false;
    }
    
    return true;
}

bool TypographyEngine::loadFontFromMemory(const uint8_t* fontData, size_t size, float fontSize) {
    this->fontSize = fontSize;
    this->fontBuffer = (uint8_t*)fontData;
    this->isEmbeddedFont = true;
    
    if (!stbtt_InitFont((stbtt_fontinfo*)fontInfo, fontBuffer, 0)) {
        printf("Failed to init embedded font\n");
        return false;
    }
    
    return true;
}

uint32_t decodeUTF8(const char* text, size_t len, size_t& i) {
    if (i >= len) return 0;
    unsigned char c = text[i];
    uint32_t codepoint = 0;
    int bytes = 0;
    
    if (c < 0x80) {
        codepoint = c;
        bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        bytes = 4;
    } else {
        i++;
        return '?';
    }
    
    i++;
    for (int b = 1; b < bytes; b++) {
        if (i >= len) break;
        codepoint = (codepoint << 6) | (text[i] & 0x3F);
        i++;
    }
    return codepoint;
}

void TypographyEngine::setFontSize(float size) {
    this->fontSize = size;
}

void TypographyEngine::setTopMargin(int pixels) {
    this->MARGIN_TOP = pixels;
}

void TypographyEngine::setBottomMargin(int pixels) {
    this->MARGIN_BOTTOM = pixels;
}

void TypographyEngine::renderText(const std::string& text, int startX, int startY, uint8_t* framebuffer, uint8_t textColor) {
    renderText(text.c_str(), text.length(), startX, startY, framebuffer, textColor);
}

void TypographyEngine::renderText(const char* text, size_t len, int startX, int startY, uint8_t* framebuffer, uint8_t textColor) {
    if (!fontBuffer) return; // Font failed to load
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)fontInfo;
    
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    
    ascent = ascent * scale;
    descent = descent * scale;
    lineGap = lineGap * scale;
    
    int x = startX;
    int y = startY + ascent;
    
    int screenWidth = DisplayHAL::getWidth();
    int screenHeight = DisplayHAL::getHeight();

    size_t i = 0;
    while (i < len) {
        // Skip trailing newline at the end of the page if it's perfectly lined up?
        // Let's just process normally.
        
        size_t wordEnd = i;
        int wordWidth = 0;
        
        // Scan the next word
        while (wordEnd < len) {
            size_t tempI = wordEnd;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            if (codepoint == ' ' || codepoint == '\n') break;
            
            int advanceWidth, leftSideBearing;
            stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
            wordWidth += (int)(advanceWidth * scale);
            wordEnd = tempI;
        }
        
        bool isSpaceOrNewline = (wordEnd == i);
        if (isSpaceOrNewline) {
            size_t tempI = i;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            if (codepoint == '\n') {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
            } else if (codepoint == ' ') {
                int advanceWidth, leftSideBearing;
                stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
                x += (int)(advanceWidth * scale);
            }
            if (y > screenHeight - MARGIN_BOTTOM) break;
            i = tempI;
            continue;
        }
        
        bool isGiantWord = wordWidth > (screenWidth - MARGIN_LEFT - MARGIN_RIGHT);
        
        // Wrap word if it doesn't fit
        if (!isGiantWord && x + wordWidth > screenWidth - MARGIN_RIGHT) {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
            
            // Note: Floating spaces at the beginning of a line are ugly.
            // If we just wrapped, the next character is this word.
            // If the preceding char was a space, we already rendered it on the previous line or ignored it.
        }
        
        if (y > screenHeight - MARGIN_BOTTOM) break;
        
        // Render the word
        while (i < wordEnd) {
            size_t tempI = i;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            
            int advanceWidth, leftSideBearing;
            stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
            int glyphWidth = (int)(advanceWidth * scale);
            
            if (isGiantWord && x + glyphWidth > screenWidth - MARGIN_RIGHT) {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
                if (y > screenHeight - MARGIN_BOTTOM) return;
            }
            
            if (stbtt_FindGlyphIndex(info, codepoint) == 0) {
                int boxWidth = 10;
                int boxHeight = ascent;
                int boxY = y - boxHeight;
                for (int by = 0; by < boxHeight; ++by) {
                    for (int bx = 0; bx < boxWidth; ++bx) {
                        if (bx == 0 || bx == boxWidth - 1 || by == 0 || by == boxHeight - 1) {
                            drawPixel(x + bx, boxY + by, textColor, framebuffer);
                        }
                    }
                }
            } else {
                int c_x1, c_y1, c_x2, c_y2;
                stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);
                
                int w = c_x2 - c_x1;
                int h = c_y2 - c_y1;
                
                if (w > 0 && h > 0) {
                    uint8_t* bitmap = stbtt_GetCodepointBitmap(info, 0, scale, codepoint, &w, &h, 0, 0);
                    for (int r = 0; r < h; ++r) {
                        for (int c = 0; c < w; ++c) {
                            uint8_t alpha = bitmap[r * w + c];
                            if (alpha > 0) {
                                int px = x + c_x1 + c;
                                int py = y + c_y1 + r;
                                uint8_t bg = getPixel(px, py, framebuffer);
                                uint8_t gray = ((textColor * alpha) + (bg * (255 - alpha))) / 255;
                                drawPixel(px, py, gray, framebuffer);
                            }
                        }
                    }
                    stbtt_FreeBitmap(bitmap, nullptr);
                }
            }
            
            x += glyphWidth;
            i = tempI;
        }
    }
}

size_t TypographyEngine::findPreviousPageStart(const std::string& text, size_t currentIndex) {
    return findPreviousPageStart(text.c_str(), text.length(), currentIndex);
}

size_t TypographyEngine::findPreviousPageStart(const char* text, size_t len, size_t currentIndex) {
    if (currentIndex == 0 || !fontBuffer) return 0;
    
    // Guess a start point backward based on an average character per page count.
    // We typically fit 500-1500 chars. We back up 2000 to be safe.
    size_t guess = (currentIndex > 2000) ? (currentIndex - 2000) : 0;
    
    // Ensure we start on a valid UTF-8 boundary
    while (guess > 0 && (text[guess] & 0xC0) == 0x80) {
        guess--;
    }
    
    size_t bestStart = guess;
    
    // Scan forward page by page from the safe previous point.
    // When the NEXT page exceeds or equals our current index, we have found the exact page start!
    while (true) {
        size_t nextStart = findNextPageStart(text, len, bestStart);
        if (nextStart >= currentIndex || nextStart == bestStart) {
            break;
        }
        bestStart = nextStart;
    }
    
    return bestStart;
}

size_t TypographyEngine::findNextPageStart(const std::string& text, size_t currentIndex) {
    return findNextPageStart(text.c_str(), text.length(), currentIndex);
}

size_t TypographyEngine::findNextPageStart(const char* text, size_t len, size_t currentIndex) {
    if (!fontBuffer) return currentIndex;
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    
    ascent = ascent * scale;
    descent = descent * scale;
    lineGap = lineGap * scale;
    
    int screenWidth = DisplayHAL::getWidth();
    int screenHeight = DisplayHAL::getHeight();

    int startX = MARGIN_LEFT;
    int startY = MARGIN_TOP;

    int x = startX;
    int y = startY + ascent;

    size_t i = currentIndex;
    
    while (i < len) {
        size_t wordEnd = i;
        int wordWidth = 0;
        
        while (wordEnd < len) {
            size_t tempI = wordEnd;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            if (codepoint == ' ' || codepoint == '\n') break;
            
            int advanceWidth, leftSideBearing;
            stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
            wordWidth += (int)(advanceWidth * scale);
            wordEnd = tempI;
        }
        
        bool isSpaceOrNewline = (wordEnd == i);
        if (isSpaceOrNewline) {
            size_t tempI = i;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            if (codepoint == '\n') {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
            } else if (codepoint == ' ') {
                int advanceWidth, leftSideBearing;
                stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
                x += (int)(advanceWidth * scale);
            }
            if (y > screenHeight - MARGIN_BOTTOM) return i;
            i = tempI;
            continue;
        }
        
        bool isGiantWord = wordWidth > (screenWidth - MARGIN_LEFT - MARGIN_RIGHT);
        
        if (!isGiantWord && x + wordWidth > screenWidth - MARGIN_RIGHT) {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
        }
        
        if (y > screenHeight - MARGIN_BOTTOM) return i;
        
        while (i < wordEnd) {
            size_t tempI = i;
            uint32_t codepoint = decodeUTF8(text, len, tempI);
            
            int advanceWidth, leftSideBearing;
            stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
            int glyphWidth = (int)(advanceWidth * scale);
            
            if (isGiantWord && x + glyphWidth > screenWidth - MARGIN_RIGHT) {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
                if (y > screenHeight - MARGIN_BOTTOM) return i;
            }
            
            x += glyphWidth;
            i = tempI;
        }
    }
    
    return i; // End of text
}

int TypographyEngine::measureText(const std::string& text) {
    return measureText(text.c_str(), text.length());
}

int TypographyEngine::measureText(const char* text, size_t len) {
    if (!fontBuffer) return 0;
    stbtt_fontinfo* info = (stbtt_fontinfo*)fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int width = 0;
    for (size_t i = 0; i < len; ) {
        uint32_t codepoint = decodeUTF8(text, len, i);
        if (codepoint == '\n') continue;
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
        width += (int)(advanceWidth * scale);
    }
    return width;
}

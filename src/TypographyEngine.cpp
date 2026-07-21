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

// We need a way to set pixels on the framebuffer
static void drawPixel(int x, int y, uint8_t color, uint8_t* framebuffer) {
#ifdef NATIVE_TESTING
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    int index = (y * EPD_WIDTH + x) / 2;
    bool isLower = (x % 2 != 0);
    uint8_t c = color & 0x0F;
    if (isLower) {
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    } else {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    }
#else
    // EPD47 specific pixel setting. We will assume the same structure.
    // epd_draw_pixel doesn't take framebuffer directly usually, wait, epdiy has it? 
    // We will do direct memory manipulation just like above for EPD47.
    // epd_driver width = 960, height = 540
    if (x < 0 || x >= 960 || y < 0 || y >= 540) return;
    int index = (y * 960 + x) / 2;
    bool isLower = (x % 2 != 0);
    uint8_t c = color & 0x0F;
    if (isLower) {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    } else {
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    }
#endif
}

static uint8_t getPixel(int x, int y, uint8_t* framebuffer) {
#ifdef NATIVE_TESTING
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return 15;
    int index = (y * EPD_WIDTH + x) / 2;
#else
    if (x < 0 || x >= 960 || y < 0 || y >= 540) return 15;
    int index = (y * 960 + x) / 2;
#endif
    bool isLower = (x % 2 != 0);
    if (isLower) {
        return framebuffer[index] & 0x0F;
    } else {
        return (framebuffer[index] & 0xF0) >> 4;
    }
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

void TypographyEngine::renderText(const std::string& text, int startX, int startY, uint8_t* framebuffer) {
    renderText(text.c_str(), text.length(), startX, startY, framebuffer);
}

void TypographyEngine::renderText(const char* text, size_t len, int startX, int startY, uint8_t* framebuffer) {
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
    
#ifdef NATIVE_TESTING
    int screenWidth = EPD_WIDTH;
    int screenHeight = EPD_HEIGHT;
#else
    int screenWidth = 960;
    int screenHeight = 540;
#endif

    for (size_t i = 0; i < len; ) {
        uint32_t codepoint = decodeUTF8(text, len, i);
        
        if (codepoint == '\n') {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
            continue;
        }
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
        
        int glyphWidth = advanceWidth * scale;
        
        if (x + glyphWidth > screenWidth - MARGIN_RIGHT) {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
        }
        
        if (y > screenHeight - MARGIN_BOTTOM) {
            break; // Screen full
        }
        
        if (stbtt_FindGlyphIndex(info, codepoint) == 0) {
            // Draw missing glyph box
            int boxWidth = 10;
            int boxHeight = ascent;
            int boxY = y - boxHeight;
            for (int by = 0; by < boxHeight; ++by) {
                for (int bx = 0; bx < boxWidth; ++bx) {
                    if (bx == 0 || bx == boxWidth - 1 || by == 0 || by == boxHeight - 1) {
                        drawPixel(x + bx, boxY + by, 0x00, framebuffer); // Black outline
                    }
                }
            }
            x += boxWidth + 2;
            continue;
        }
        
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
                        // Blend: bg * (255 - alpha) / 255 (since text is black/0)
                        uint8_t gray = (bg * (255 - alpha)) / 255;
                        drawPixel(px, py, gray, framebuffer);
                    }
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        
        x += advanceWidth * scale;
    }
}

size_t TypographyEngine::findPreviousPageStart(const std::string& text, size_t currentIndex) {
    return findPreviousPageStart(text.c_str(), text.length(), currentIndex);
}

size_t TypographyEngine::findPreviousPageStart(const char* text, size_t len, size_t currentIndex) {
    if (!fontBuffer || currentIndex == 0) return 0;
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    
    ascent = ascent * scale;
    descent = descent * scale;
    lineGap = lineGap * scale;
    
#ifdef NATIVE_TESTING
    int screenWidth = EPD_WIDTH;
    int screenHeight = EPD_HEIGHT;
#else
    int screenWidth = 960;
    int screenHeight = 540;
#endif

    int y = screenHeight - descent; // Start from bottom
    int x = screenWidth;
    
    size_t i = currentIndex;
    
    // We need to parse backwards. To do this properly with UTF-8, we just decode everything up to currentIndex.
    // For simplicity, since we are moving backwards, we can step backwards character by character.
    // But since line wrapping is forward-dependent (a long word wraps completely), a true exact reverse layout
    // actually requires laying out forward from known paragraph starts, or just doing a binary search on the page.
    // A simplified exact reverse: just step backwards and subtract advanceWidth. 
    // It's an approximation of the exact forward layout.
    // However, the exact algorithm "forward layout from top-left, reverse layout from bottom-right" usually implies
    // doing the layout backwards word by word.
    
    while (i > 0) {
        i--;
        // backtrack UTF-8
        while (i > 0 && (text[i] & 0xC0) == 0x80) {
            i--;
        }
        
        size_t tempI = i;
        uint32_t codepoint = decodeUTF8(text, len, tempI);
        
        if (codepoint == '\n') {
            x = screenWidth - MARGIN_RIGHT;
            y -= (ascent - descent + lineGap + LINE_SPACING);
            if (y < MARGIN_TOP + ascent) return i + 1; // Reached top
            continue;
        }
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
        int glyphWidth = advanceWidth * scale;
        
        if (x - glyphWidth < MARGIN_LEFT) {
            x = screenWidth - MARGIN_RIGHT - glyphWidth;
            y -= (ascent - descent + lineGap + LINE_SPACING);
            if (y < MARGIN_TOP + ascent) {
                // This character pushed us over the top margin.
                return tempI; // the index after the current char, wait, tempI is the start of NEXT char
                // Actually if this character doesn't fit, the page starts at this character? No, if it doesn't fit on top, it belongs to the previous-previous page.
                // So the previous page starts at the character AFTER this one.
                // tempI is the index of the next character in string.
                // Wait, if it pushed us over, we stop.
            }
        } else {
            x -= glyphWidth;
        }
        
        if (y < MARGIN_TOP + ascent) {
            // we already passed the top. Return the character that would be the first one on the page.
            return tempI; // The character that caused overflow is left on the previous-previous page.
        }
    }
    
    return 0;
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

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
        framebuffer[index] = (framebuffer[index] & 0xF0) | c;
    } else {
        framebuffer[index] = (framebuffer[index] & 0x0F) | (c << 4);
    }
#endif
}

TypographyEngine::TypographyEngine() : fontBuffer(nullptr), fontSize(16.0f), fontInfo(nullptr) {
    fontInfo = malloc(sizeof(stbtt_fontinfo));
}

TypographyEngine::~TypographyEngine() {
    if (fontBuffer) {
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

// Simple UTF-8 decoder
static uint32_t decodeUTF8(const std::string& text, size_t& i) {
    uint32_t codepoint = 0;
    uint8_t c = text[i];
    if (c <= 0x7F) {
        codepoint = c;
    } else if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        if (i + 1 < text.length()) {
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
        }
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        if (i + 2 < text.length()) {
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
        }
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        if (i + 3 < text.length()) {
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
            codepoint = (codepoint << 6) | (text[++i] & 0x3F);
        }
    }
    return codepoint;
}

void TypographyEngine::renderText(const std::string& text, int startX, int startY, uint8_t* framebuffer) {
    stbtt_fontinfo* info = (stbtt_fontinfo*)fontInfo;
    
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    
    ascent = ascent * scale;
    descent = descent * scale;
    lineGap = lineGap * scale;
    
    int x = startX + MARGIN_LEFT;
    int y = startY + MARGIN_TOP + ascent;
    
    // Simple screen bounds
#ifdef NATIVE_TESTING
    int screenWidth = EPD_WIDTH;
    int screenHeight = EPD_HEIGHT;
#else
    int screenWidth = 960;
    int screenHeight = 540;
#endif

    for (size_t i = 0; i < text.length(); ++i) {
        uint32_t codepoint = decodeUTF8(text, i);
        
        if (codepoint == '\n') {
            x = startX + MARGIN_LEFT;
            y += (ascent - descent + lineGap + LINE_SPACING);
            continue;
        }
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftSideBearing);
        
        int glyphWidth = advanceWidth * scale;
        
        if (x + glyphWidth > screenWidth - MARGIN_RIGHT) {
            x = startX + MARGIN_LEFT;
            y += (ascent - descent + lineGap + LINE_SPACING);
        }
        
        if (y > screenHeight - MARGIN_BOTTOM) {
            // Reached bottom, stop pagination
            break;
        }
        
        int glyphIndex = stbtt_FindGlyphIndex(info, codepoint);
        if (glyphIndex == 0) {
            // Missing glyph placeholder
            int boxWidth = fontSize * 0.5f;
            int boxHeight = fontSize * 0.8f;
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
                    if (alpha > 128) {
                        // For 4-bit grayscale, 0x00 is black, 0x0F is white.
                        // We do a simple thresholding for black pixels.
                        drawPixel(x + c_x1 + c, y + c_y1 + r, 0x00, framebuffer);
                    } else if (alpha > 0) {
                        // Add some basic anti-aliasing approximation (e.g. gray 0x08)
                        uint8_t gray = 0x0F - (alpha >> 4);
                        // However, we only draw if it's darker than white, to not overwrite other things?
                        // Just overwrite. Wait, we want to blend. Let's keep it simple: draw gray.
                        drawPixel(x + c_x1 + c, y + c_y1 + r, gray, framebuffer);
                    }
                }
            }
            stbtt_FreeBitmap(bitmap, info->userdata);
        }
        
        x += advanceWidth * scale;
    }
}

size_t TypographyEngine::findPreviousPageStart(const std::string& text, size_t currentIndex) {
    if (currentIndex == 0 || text.empty()) return 0;
    
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

    int y = screenHeight - MARGIN_BOTTOM - descent; // Start from bottom
    int x = screenWidth - MARGIN_RIGHT;
    
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
        uint32_t codepoint = decodeUTF8(text, tempI);
        
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

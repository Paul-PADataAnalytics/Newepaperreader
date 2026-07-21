#include "UIFramework.h"
#include "DisplayHAL.h"

void UIFramework::init() {
    // Placeholder for loading fonts or other UI assets if needed
}

void UIFramework::drawTopBar(uint8_t *framebuffer, const char* title, int batteryPercent) {
    // Draw a top bar background (light gray or line underneath)
    DisplayHAL::drawHLine(0, 30, EPD_WIDTH, 0x00, framebuffer); // Black line at y=30
    
    // Placeholder for text rendering (needs font header inclusion)
    // writeln((GFXfont *)&firasans, title, &cursor_x, &cursor_y, framebuffer);
}

void UIFramework::drawProgressBar(uint8_t *framebuffer, float progress, const char* label) {
    int barWidth = EPD_WIDTH - 40;
    int barHeight = 8;
    int x = 20;
    int y = EPD_HEIGHT - 20;
    
    // Outline
    DisplayHAL::drawRect(x, y, barWidth, barHeight, 0x00, framebuffer);
    // Fill based on progress (0.0 to 1.0)
    int fillWidth = (int)(barWidth * progress);
    DisplayHAL::fillRect(x, y, fillWidth, barHeight, 0x00, framebuffer);
}

void UIFramework::drawButton(uint8_t *framebuffer, int x, int y, int w, int h, const char* label, bool inverted) {
    uint8_t bgColor = inverted ? 0x00 : 0xBB; // Black if inverted, 2 steps darker than lightest grey (0xDD) otherwise
    uint8_t fgColor = inverted ? 0xFF : 0x00;
    
    // Background
    DisplayHAL::fillRect(x, y, w, h, bgColor, framebuffer);
    // Border
    DisplayHAL::drawRect(x, y, w, h, fgColor, framebuffer);
    
    // Text placeholder...
}

void UIFramework::clearArea(uint8_t *framebuffer, int x, int y, int w, int h) {
    DisplayHAL::fillRect(x, y, w, h, 0xFF, framebuffer); // 0xFF is white
}

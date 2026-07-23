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
    uint8_t bgColor = inverted ? 0x00 : 0xEE; // Black if inverted, very light grey (masks to 0x0E) otherwise
    uint8_t fgColor = inverted ? 0xFF : 0x00;
    
    // Background
    DisplayHAL::fillRect(x, y, w, h, bgColor, framebuffer);
    // Border
    DisplayHAL::drawRect(x, y, w, h, fgColor, framebuffer);
    
    // Text placeholder...
}

uint8_t UIFramework::getBackgroundColor() {
    return 0xFF; // Always white in memory, DisplayHAL handles dark mode inversion
}

uint8_t UIFramework::getForegroundColor() {
    return 0x00; // Always black in memory
}

void UIFramework::clearArea(uint8_t *framebuffer, int x, int y, int w, int h) {
    DisplayHAL::fillRect(x, y, w, h, getBackgroundColor(), framebuffer);
}

void UIFramework::clearToBackground(uint8_t *framebuffer) {
    if (!framebuffer) return;
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();
    clearArea(framebuffer, 0, 0, w, h);
}

void UIFramework::perform2PassPartialUpdate(uint8_t *framebuffer, int x, int y, int w, int h, std::function<void()> renderContent) {
    if (!framebuffer) return;

    // Clear localized bounding box in memory
    clearArea(framebuffer, x, y, w, h);

    // Render new content into cleared region
    if (renderContent) {
        renderContent();
    }
    
    // Let DisplayHAL perform diff-based partial update with auto-clear
    DisplayHAL::updateScreenPartial();
}

void UIFramework::performFastPartialUpdate(uint8_t* framebuffer, int x, int y, int w, int h, std::function<void()> drawFunc) {
    if (!framebuffer) return;

    // Clear localized bounding box in memory
    clearArea(framebuffer, x, y, w, h);

    if (drawFunc) {
        drawFunc();
    }
    
    DisplayHAL::updateScreenFast();
}

void UIFramework::performFullScreenDraw(uint8_t *framebuffer, std::function<void()> renderContent) {
    if (!framebuffer) return;

    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    // 1. Clear entire framebuffer to background color in memory
    clearArea(framebuffer, 0, 0, w, h);

    // 2. Render new screen content into the clean framebuffer
    if (renderContent) {
        renderContent();
    }

    // 3. Delegate to DisplayHAL for a full hardware clear and buffer swap
    DisplayHAL::updateScreenFull();
}

void UIFramework::drawIcon16x16(uint8_t *framebuffer, int x, int y, const uint8_t *bitmap, uint8_t color) {
    for (int r = 0; r < 16; r++) {
        uint16_t rowData = (bitmap[r * 2] << 8) | bitmap[r * 2 + 1];
        for (int c = 0; c < 16; c++) {
            if ((rowData & (1 << (15 - c))) != 0) {
                DisplayHAL::setPixel(x + c, y + r, color, framebuffer);
            }
        }
    }
}

const uint8_t COG_ICON[32] = {
    0x06, 0x60, //    ##   ##
    0x0f, 0xf0, //   ########
    0x1f, 0xf8, //  ##########
    0x37, 0xec, //  ## ## ## ##
    0x77, 0xee, // ### ## ## ###
    0xff, 0xff, // ############
    0xdb, 0xdb, // ## ##  ## ##
    0x99, 0x99, // #  ##  ##  #
    0x99, 0x99, // #  ##  ##  #
    0xdb, 0xdb, // ## ##  ## ##
    0xff, 0xff, // ############
    0x77, 0xee, // ### ## ## ###
    0x37, 0xec, //  ## ## ## ##
    0x1f, 0xf8, //  ##########
    0x0f, 0xf0, //   ########
    0x06, 0x60  //    ##   ##
};

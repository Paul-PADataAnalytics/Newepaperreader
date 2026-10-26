#include "TypographyEngine.h"
extern TypographyEngine typography;
#include "UIFramework.h"
#include "DisplayHAL.h"
#include "UIKit.h"
#include <time.h>
#include <cstdio>

void UIFramework::init() {
    // Placeholder for loading fonts or other UI assets if needed
}

// Single source of truth for the "live system status" text (current time +
// battery). Apps must call this instead of hardcoding time/battery strings.
// Renders right-aligned so its right edge sits at `rightX`.
void UIFramework::drawSystemStatus(uint8_t *framebuffer, int rightX, int y, float fontSize, uint8_t color, bool includeDate) {
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    char timeStr[32];
    if (timeinfo && timeinfo->tm_year > 70) {
        strftime(timeStr, sizeof(timeStr), includeDate ? "%a %b %d | %I:%M %p" : "%I:%M %p", timeinfo);
    } else {
        snprintf(timeStr, sizeof(timeStr), includeDate ? "No Time Set" : "--:--");
    }

    int battPct = DisplayHAL::getBatteryPercent();
    const char* chargeIcon = DisplayHAL::isCharging() ? "+" : "";

    char statusStr[64];
    snprintf(statusStr, sizeof(statusStr), "%s | %d%%%s", timeStr, battPct, chargeIcon);

    typography.setFontSize(fontSize);
    int tw = typography.measureText(statusStr);
    typography.renderText(statusStr, rightX - tw, y, framebuffer, color);
}

void UIFramework::drawTopBar(uint8_t *framebuffer, const char* title) {
    (void)framebuffer;
    UI::header(title ? title : "", true);
}

void UIFramework::drawProgressBar(uint8_t *framebuffer, float progress, const char* label) {
    (void)framebuffer;
    (void)label;
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();
    UI::progressBar(UI::MARGIN, h - 24, w - 2 * UI::MARGIN, 6, progress);
}

void UIFramework::drawButton(uint8_t *framebuffer, int x, int y, int w, int h, const char* label, bool inverted) {
    (void)framebuffer;
    UI::button(UI::Rect(x, y, w, h), label ? label : "",
               inverted ? UI::ButtonStyle::PRIMARY : UI::ButtonStyle::SECONDARY);
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

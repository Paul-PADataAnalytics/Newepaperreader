#include "AppScreens.h"
#include "UIFramework.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"

extern TypographyEngine typography;

namespace AppScreens {

void drawLoading(uint8_t* framebuffer, const char* title) {
    UIFramework::performFullScreenDraw(framebuffer, [framebuffer, title]() {
        uint8_t fg = UIFramework::getForegroundColor();
        UIFramework::drawTopBar(framebuffer, title, 100);
        typography.setFontSize(32.0f);
        int tw = typography.measureText("Loading...");
        int w = DisplayHAL::getWidth();
        typography.renderText("Loading...", (w - tw) / 2, 200, framebuffer, fg);
    });
}

} // namespace AppScreens

#include "AppScreens.h"
#include "UIFramework.h"
#include "UIKit.h"

namespace AppScreens {

void drawLoading(uint8_t* framebuffer, const char* title) {
    UIFramework::performFullScreenDraw(framebuffer, [title]() {
        using namespace UI;
        statusBar();
        textCentered("Opening", W / 2, H / 2 - 70, FONT_SMALL, DARK);
        textWrapped(title ? title : "", MARGIN * 2, H / 2 - 30, W - MARGIN * 4, FONT_TITLE, BLACK, 3, 6, true);
    });
}

} // namespace AppScreens

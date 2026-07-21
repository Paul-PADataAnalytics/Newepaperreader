#include "AppScreens.h"
#include "UIFramework.h"
#include "DisplayHAL.h"

namespace AppScreens {

void drawLoading(uint8_t* framebuffer, const char* title) {
    UIFramework::clearArea(framebuffer, 0, 0, 960, 540);
    UIFramework::drawTopBar(framebuffer, title, 100);
    DisplayHAL::display(framebuffer);
}

} // namespace AppScreens

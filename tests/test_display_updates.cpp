#include "DisplayHAL.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main() {
    DisplayHAL::frontBuffer = DisplayHAL::allocateFramebuffer();
    DisplayHAL::backBuffer = DisplayHAL::allocateFramebuffer();
    assert(DisplayHAL::frontBuffer && DisplayHAL::backBuffer);
    const size_t bytes = EPD_WIDTH * EPD_HEIGHT / 2;
    memset(DisplayHAL::frontBuffer, 255, bytes);
    memset(DisplayHAL::backBuffer, 255, bytes);

    for (bool portrait : {false, true}) {
        DisplayHAL::setPortrait(portrait);
        for (bool dark : {false, true}) {
            DisplayHAL::setDarkMode(dark);
            DisplayHAL::updateScreenFull();
            DisplayHAL::setPixel(11, 17, 3, DisplayHAL::frontBuffer);
            DisplayHAL::setPixel(12, 17, 5, DisplayHAL::frontBuffer);
            DisplayHAL::updateScreenPartial();
            assert(DisplayHAL::getPixel(11, 17, DisplayHAL::backBuffer) == 3);
            assert(DisplayHAL::getPixel(12, 17, DisplayHAL::backBuffer) == 5);

            // Same monochrome threshold, different gray: quality partials
            // must still present this change and preserve neighboring pixels.
            DisplayHAL::setPixel(11, 17, 6, DisplayHAL::frontBuffer);
            DisplayHAL::updateScreenPartial();
            assert(DisplayHAL::getPixel(11, 17, DisplayHAL::backBuffer) == 6);
            assert(DisplayHAL::getPixel(12, 17, DisplayHAL::backBuffer) == 5);
            DisplayHAL::updateScreenPartial();
            assert(memcmp(DisplayHAL::frontBuffer, DisplayHAL::backBuffer, bytes) == 0);
            DisplayHAL::fillRect(0, 0, DisplayHAL::getWidth(), DisplayHAL::getHeight(),
                                 255, DisplayHAL::frontBuffer);
            DisplayHAL::updateScreenFast();
            assert(memcmp(DisplayHAL::frontBuffer, DisplayHAL::backBuffer, bytes) == 0);
        }
    }
    DisplayHAL::freeFramebuffer(DisplayHAL::frontBuffer);
    DisplayHAL::freeFramebuffer(DisplayHAL::backBuffer);
    DisplayHAL::frontBuffer = nullptr;
    DisplayHAL::backBuffer = nullptr;
    std::puts("Display update regression checks passed");
}

#include <iostream>
#include <string>
#include "DisplayHAL.h"
#include "TypographyEngine.h"

int main() {
    std::cout << "Starting test_typ2..." << std::endl;
    DisplayHAL::init();
    DisplayHAL::setPortrait(false);

    TypographyEngine typo;
    typo.init();
    typo.setFontSize(28.0f);
    
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();
    typo.setTopMargin(60);
    typo.setBottomMargin(h * 0.05);

    std::string text = "When i moved backwards in a book, it moved back 1 paragraph, not a whole page so the layout algorithm isn't use the work before the first word in the bottom, this was on rotated view of the ebook page, ensure this is accounted for when doing layouts. you should perform simulations using the native app to validate your algoritms. ";
    for (int i=0; i<100; i++) {
        text += "Word " + std::to_string(i) + " to fill the page. ";
    }
    
    size_t p1 = 0;
    std::cout << "Testing findNextPageStart from 0...\n";
    size_t nextStart = typo.findNextPageStart(text.c_str(), text.length(), p1);
    std::cout << "findNextPageStart returned: " << nextStart << "\n";
    
    std::cout << "Testing renderTextPaged from 0...\n";
    uint8_t dummy_fb[960 * 540] = {0};
    typo.renderTextPaged(text.c_str(), text.length(), p1, dummy_fb, 0);

    return 0;
}

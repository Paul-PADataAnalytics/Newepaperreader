#include <iostream>
#include <string>
#include <vector>

// Mocks
int screenWidth = 960;
int screenHeight = 540;
int MARGIN_LEFT = 20;
int MARGIN_RIGHT = 20;
int MARGIN_TOP = 60;
int MARGIN_BOTTOM = 27; // 540 * 0.05
int ascent = 20;
int descent = -5;
int lineGap = 5;
int LINE_SPACING = 2;

size_t findNextPageStart(const std::string& text, size_t currentIndex) {
    size_t len = text.length();
    int startX = MARGIN_LEFT;
    int startY = MARGIN_TOP;

    int x = startX;
    int y = startY + ascent;

    size_t i = currentIndex;

    while (i < len) {
        size_t wordEnd = i;
        int wordWidth = 0;

        while (wordEnd < len) {
            char codepoint = text[wordEnd];
            if (codepoint == ' ' || codepoint == '\n') break;
            wordWidth += 10;
            wordEnd++;
        }

        bool isSpaceOrNewline = (wordEnd == i);
        if (isSpaceOrNewline) {
            char codepoint = text[i];
            if (codepoint == '\n') {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
            } else if (codepoint == ' ') {
                x += 10;
            }
            if (y > screenHeight - MARGIN_BOTTOM) return i;
            i++;
            continue;
        }

        bool isGiantWord = wordWidth > (screenWidth - MARGIN_LEFT - MARGIN_RIGHT);

        if (!isGiantWord && x + wordWidth > screenWidth - MARGIN_RIGHT) {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
        }

        if (y > screenHeight - MARGIN_BOTTOM) return i;

        while (i < wordEnd) {
            char codepoint = text[i];
            int glyphWidth = 10;

            if (isGiantWord && x + glyphWidth > screenWidth - MARGIN_RIGHT) {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
                if (y > screenHeight - MARGIN_BOTTOM) return i;
            }

            x += glyphWidth;
            i++;
        }
    }

    return i;
}

std::vector<size_t> renderTextPaged(const std::string& text, size_t currentIndex) {
    std::vector<size_t> renderedIndices;
    size_t len = text.length();
    int startX = MARGIN_LEFT;
    int startY = MARGIN_TOP;

    int x = startX;
    int y = startY + ascent;

    size_t i = currentIndex;
    while (i < len) {
        size_t wordEnd = i;
        int wordWidth = 0;

        while (wordEnd < len) {
            char codepoint = text[wordEnd];
            if (codepoint == ' ' || codepoint == '\n') break;
            wordWidth += 10;
            wordEnd++;
        }

        bool isSpaceOrNewline = (wordEnd == i);
        if (isSpaceOrNewline) {
            char codepoint = text[i];
            if (codepoint == '\n') {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
            } else if (codepoint == ' ') {
                x += 10;
            }
            if (y > screenHeight - MARGIN_BOTTOM) break;
            renderedIndices.push_back(i);
            i++;
            continue;
        }

        bool isGiantWord = wordWidth > (screenWidth - MARGIN_LEFT - MARGIN_RIGHT);

        if (!isGiantWord && x + wordWidth > screenWidth - MARGIN_RIGHT) {
            x = startX;
            y += (ascent - descent + lineGap + LINE_SPACING);
        }

        if (y > screenHeight - MARGIN_BOTTOM) break;

        while (i < wordEnd) {
            char codepoint = text[i];
            int glyphWidth = 10;

            if (isGiantWord && x + glyphWidth > screenWidth - MARGIN_RIGHT) {
                x = startX;
                y += (ascent - descent + lineGap + LINE_SPACING);
                if (y > screenHeight - MARGIN_BOTTOM) return renderedIndices;
            }

            renderedIndices.push_back(i);
            x += glyphWidth;
            i++;
        }
    }
    return renderedIndices;
}

int main() {
    std::string text = "";
    for(int i=0; i<1000; i++) text += "Word" + std::to_string(i) + " ";
    
    size_t p1 = 0;
    size_t nextStart = findNextPageStart(text, p1);
    
    std::vector<size_t> rendered = renderTextPaged(text, p1);
    
    std::cout << "findNextPageStart says next page starts at: " << nextStart << std::endl;
    std::cout << "renderTextPaged rendered " << rendered.size() << " chars. Last rendered index: " << rendered.back() << std::endl;
    std::cout << "Missing chars: " << nextStart - 1 - rendered.back() << std::endl;

    return 0;
}

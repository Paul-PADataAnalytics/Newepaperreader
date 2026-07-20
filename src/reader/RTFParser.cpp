#include "RTFParser.h"

std::string RTFParser::stripRTF(const std::string& rtf) {
    std::string out;
    size_t i = 0;
    int groupDepth = 0;
    // We will just do a very crude parsing: 
    // Ignore control words. 
    while (i < rtf.length()) {
        char c = rtf[i];
        if (c == '{') {
            groupDepth++;
            i++;
        } else if (c == '}') {
            if (groupDepth > 0) groupDepth--;
            i++;
        } else if (c == '\\') {
            i++;
            if (i < rtf.length()) {
                if (rtf[i] == '\\' || rtf[i] == '{' || rtf[i] == '}') {
                    out += rtf[i]; // escaped character
                    i++;
                } else if (rtf[i] == '\'') {
                    // Hex character
                    i += 3;
                } else {
                    // Control word: read letters, then maybe digits, then maybe a space
                    while (i < rtf.length() && isalpha(rtf[i])) {
                        i++;
                    }
                    while (i < rtf.length() && (isdigit(rtf[i]) || rtf[i] == '-')) {
                        i++;
                    }
                    if (i < rtf.length() && rtf[i] == ' ') {
                        i++; // space after control word is part of it
                    }
                }
            }
        } else if (c == '\r' || c == '\n') {
            i++; // RTF ignores newlines in code
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

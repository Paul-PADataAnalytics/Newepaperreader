#include "RTFParser.h"

std::string RTFParser::stripRTF(const std::string& rtf) {
    std::string out;
    size_t i = 0;
    int groupDepth = 0;
    int skipGroupDepth = -1;
    
    while (i < rtf.length()) {
        char c = rtf[i];
        if (c == '{') {
            groupDepth++;
            i++;
        } else if (c == '}') {
            if (skipGroupDepth != -1 && groupDepth == skipGroupDepth) {
                skipGroupDepth = -1;
            }
            if (groupDepth > 0) groupDepth--;
            i++;
        } else if (c == '\\') {
            i++;
            if (i < rtf.length()) {
                if (rtf[i] == '\\' || rtf[i] == '{' || rtf[i] == '}') {
                    if (skipGroupDepth == -1) out += rtf[i];
                    i++;
                } else if (rtf[i] == '\'') {
                    i += 3;
                } else {
                    std::string ctrl = "";
                    if (i < rtf.length() && !isalpha(rtf[i])) {
                        ctrl += rtf[i];
                        i++;
                    } else {
                        while (i < rtf.length() && isalpha(rtf[i])) {
                            ctrl += rtf[i];
                            i++;
                        }
                        while (i < rtf.length() && (isdigit(rtf[i]) || rtf[i] == '-')) {
                            i++;
                        }
                        if (i < rtf.length() && rtf[i] == ' ') {
                            i++;
                        }
                    }
                    if (ctrl == "*" || ctrl == "fonttbl" || ctrl == "colortbl" || ctrl == "stylesheet" || ctrl == "info") {
                        if (skipGroupDepth == -1) skipGroupDepth = groupDepth;
                    } else if (ctrl == "par" || ctrl == "line") {
                        if (skipGroupDepth == -1) out += '\n';
                    }
                }
            }
        } else if (c == '\r' || c == '\n') {
            i++;
        } else {
            if (skipGroupDepth == -1) {
                out += c;
            }
            i++;
        }
    }
    return out;
}

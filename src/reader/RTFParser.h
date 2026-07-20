#pragma once
#include <string>

class RTFParser {
public:
    static std::string stripRTF(const std::string& rtfText);
};

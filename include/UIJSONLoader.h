#pragma once

#include "HeroAdventureApp.h"
#include <string>
#include <memory>

class UIJSONLoader {
public:
    UIJSONLoader() = default;
    ~UIJSONLoader() = default;

    // Load a UI screen definition from SD card JSON file
    // Returns true if successful
    bool loadScreenFromFile(const std::string& screenId, UIScreen& outScreen);
    
    // Initialize - loads base path and validates SD card access
    bool init(const std::string& basePath = "/sd/games/hero_adventure/ui");
    
private:
    std::string basePath;
    static const int MAX_JSON_SIZE = 8192;
    
    // Helper to parse JSON and extract screen data
    bool parseScreenJSON(const std::string& jsonContent, UIScreen& outScreen);
    bool parseButtons(const std::string& jsonContent, std::vector<UIButton>& outButtons);
    bool parseTextLines(const std::string& jsonContent, std::vector<std::string>& outLines);
};

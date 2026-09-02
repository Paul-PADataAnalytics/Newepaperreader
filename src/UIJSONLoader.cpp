#include "UIJSONLoader.h"
#include <cstring>
#include <ArduinoJson.h>
#include <regex>

#ifndef NATIVE_TESTING
#include <SD.h>
#include <Arduino.h>
#else
#include <fstream>
#include <sstream>
#endif

bool UIJSONLoader::init(const std::string& path) {
    basePath = path;
#ifdef NATIVE_TESTING
    // In native testing, adjust path to project directory
    std::string projPath = "games/hero_adventure/ui";
    basePath = projPath;
#endif
    return true;
}

bool UIJSONLoader::loadScreenFromFile(const std::string& screenId, UIScreen& outScreen) {
#ifdef NATIVE_TESTING
    // In test mode, load from local filesystem
    // Try multiple possible paths to handle different working directories
    std::vector<std::string> possiblePaths = {
        basePath + "/" + screenId + ".json",
        "../games/hero_adventure/ui/" + screenId + ".json",
        "../../games/hero_adventure/ui/" + screenId + ".json",
    };
    
    for (const auto& filePath : possiblePaths) {
        std::ifstream file(filePath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string jsonContent = buffer.str();
            file.close();
            return parseScreenJSON(jsonContent, outScreen);
        }
    }
    
    // If we get here, file wasn't found in any path
    return false;
#else
    // In firmware mode, load from SD card
    std::string filePath = basePath + "/" + screenId + ".json";
    File file = SD.open(filePath.c_str(), FILE_READ);
    if (!file) {
        return false;
    }
    
    size_t size = file.size();
    if (size > MAX_JSON_SIZE) {
        file.close();
        return false;
    }
    
    char* jsonBuffer = (char*)malloc(size + 1);
    if (!jsonBuffer) {
        file.close();
        return false;
    }
    
    file.read((uint8_t*)jsonBuffer, size);
    jsonBuffer[size] = '\0';
    file.close();
    
    std::string jsonContent(jsonBuffer);
    free(jsonBuffer);
    
    return parseScreenJSON(jsonContent, outScreen);
#endif
}

bool UIJSONLoader::parseScreenJSON(const std::string& jsonContent, UIScreen& outScreen) {
    StaticJsonDocument<8192> doc;
    DeserializationError error = deserializeJson(doc, jsonContent);
    
    if (error) {
        return false;
    }
    
    // Extract screen metadata
    if (doc.containsKey("id")) {
        outScreen.id = doc["id"].as<std::string>();
    }
    
    if (doc.containsKey("title")) {
        outScreen.title = doc["title"].as<std::string>();
        outScreen.titleOriginal = outScreen.title;
        // Strip leading emoji/non-ASCII icons that the device font may not support
        try {
            static const std::regex stripLead("^([^\\x00-\\x7F]+)\\s*");
            outScreen.title = std::regex_replace(outScreen.title, stripLead, std::string(""));
        } catch(...) {
            // Regex may not be supported in some embedded environments; ignore if it fails
        }
    }
    
    // Extract text lines
    // Layout metadata (optional)
    if (doc.containsKey("layout")) {
        outScreen.layout = doc["layout"].as<std::string>();
    }
    if (doc.containsKey("left_ratio")) {
        // left_ratio may be numeric
        try {
            outScreen.leftRatio = static_cast<float>(doc["left_ratio"].as<double>());
        } catch(...) {}
    }
    if (doc.containsKey("no_wrap")) {
        try {
            outScreen.noWrap = doc["no_wrap"].as<bool>();
        } catch(...) {}
    }

    // Extract text lines. Support either simple string entries (legacy) or
    // object entries with { "text": "...", "col": "left|right" } for
    // two-column layout.
    if (doc.containsKey("text") && doc["text"].is<JsonArray>()) {
        JsonArray textArray = doc["text"].as<JsonArray>();
        outScreen.textLines.clear();
        outScreen.textLinesOriginal.clear();
        outScreen.textLinesLeftOriginal.clear();
        outScreen.textLinesRightOriginal.clear();

        for (const auto& line : textArray) {
            if (line.is<JsonObject>()) {
                JsonObject obj = line.as<JsonObject>();
                if (obj.containsKey("text")) {
                    std::string txt = obj["text"].as<std::string>();
                    std::string col = obj.containsKey("col") ? obj["col"].as<std::string>() : "left";
                    if (col == "right") {
                        outScreen.textLinesRightOriginal.push_back(txt);
                    } else {
                        outScreen.textLinesLeftOriginal.push_back(txt);
                    }
                }
            } else {
                std::string lineStr = line.as<std::string>();
                outScreen.textLines.push_back(lineStr);
                outScreen.textLinesOriginal.push_back(lineStr);  // Save original
            }
        }
    }
    
    // Extract buttons
    if (doc.containsKey("buttons") && doc["buttons"].is<JsonArray>()) {
        JsonArray buttonsArray = doc["buttons"].as<JsonArray>();
        outScreen.buttons.clear();
        outScreen.buttonsOriginal.clear();
        
        for (const auto& btnObj : buttonsArray) {
            UIButton btn;
            btn.label = btnObj["label"].as<std::string>();
            // Normalize/remove unsupported emoji prefixes for device font
            try {
                static const std::regex stripLeadBtn("^([^\\x00-\\x7F]+)\\s*");
                btn.label = std::regex_replace(btn.label, stripLeadBtn, std::string(""));
            } catch(...) {}
            btn.action = btnObj["action"].as<std::string>();
            if (btnObj.containsKey("visible_if")) {
                btn.visibleIf = btnObj["visible_if"].as<std::string>();
            }
            
            // Default button dimensions - will be calculated during rendering
            btn.x = 0;
            btn.y = 0;
            btn.w = 0;
            btn.h = 0;
            
            outScreen.buttons.push_back(btn);
            outScreen.buttonsOriginal.push_back(btn);  // Save full set for visible_if re-evaluation
        }
    }
    
    return true;
}

bool UIJSONLoader::parseButtons(const std::string& jsonContent, std::vector<UIButton>& outButtons) {
    // This is handled in parseScreenJSON, kept for compatibility
    return true;
}

bool UIJSONLoader::parseTextLines(const std::string& jsonContent, std::vector<std::string>& outLines) {
    // This is handled in parseScreenJSON, kept for compatibility
    return true;
}

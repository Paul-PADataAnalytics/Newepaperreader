#pragma once

#include <string>
#include <vector>
#include <cstdint>

class AppComm {
public:
    static void init();
    static void deinit();
    static void poll();
    static void sendData(const std::string& data);
    static bool hasData();
    static std::string getNextMessage();
    static void setReadBuffer(const std::string& data);
    static std::string getReadBuffer();
    static bool isInitialized();

    // File upload helpers used by SettingsApp to receive chunked files over BLE.
    static void startFileUpload(const std::string& name, int totalChunks);
    static void appendFileChunk(int chunkIndex, const std::vector<uint8_t>& data);
    static bool finishFileUpload();
    static void cancelFileUpload();
};

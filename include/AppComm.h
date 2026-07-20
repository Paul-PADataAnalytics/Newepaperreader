#pragma once

#include <string>

class AppComm {
public:
    static void init();
    static void poll();
    static void sendData(const std::string& data);
    static bool hasData();
    static std::string getNextMessage();
};

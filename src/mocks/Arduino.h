#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <stdarg.h>

// Mock Serial
class MockSerial {
public:
    void begin(int baud) {}
    void println(const char* s) { printf("%s\n", s); }
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
};

extern MockSerial Serial;

inline void delay(unsigned long ms) {
    // nothing
}

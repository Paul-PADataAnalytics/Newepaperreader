#pragma once
#include <string>

class File {
public:
    File() : valid(false) {}
    File(bool v) : valid(v) {}
    operator bool() const { return valid; }
    
    bool isDirectory() { return false; }
    File openNextFile() { return File(false); }
    const char* name() { return ""; }
    size_t size() { return 0; }
    
    // For TextReader
    size_t read(uint8_t *buf, size_t size) { return 0; }
    bool seek(uint32_t pos) { return false; }
    uint32_t position() { return 0; }
    void close() { valid = false; }
    
private:
    bool valid;
};

class SDFS {
public:
    File open(const char* path, const char* mode = "r") {
        return File(false);
    }
};

extern SDFS SD;

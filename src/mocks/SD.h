#pragma once
#include <string>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <dirent.h>

#define FILE_READ "r"

class File {
public:
    File() : f(nullptr), dir(nullptr), valid(false) {}
    File(FILE* file) : f(file), dir(nullptr), valid(file != nullptr), _isDir(false) {}
    File(DIR* directory, const char* p) : f(nullptr), dir(directory), valid(directory != nullptr), _isDir(true), path(p) {}
    operator bool() const { return valid; }

    bool isDirectory() { return _isDir; }
    
    File openNextFile() {
        if (!dir) return File();
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            
            // Simple mock file
            File mockFile;
            mockFile.valid = true;
            mockFile._isDir = (ent->d_type == DT_DIR);
            mockFile.fileName = ent->d_name;
            return mockFile;
        }
        return File();
    }
    
    const char* name() { return fileName.c_str(); }
    
    size_t size() {
        if (!f) return 0;
        long current = ftell(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, current, SEEK_SET);
        return size;
    }

    // For TextReader
    size_t read(uint8_t *buf, size_t size) {
        if (!f) return 0;
        return fread(buf, 1, size, f);
    }
    bool seek(uint32_t pos) {
        if (!f) return false;
        return fseek(f, pos, SEEK_SET) == 0;
    }
    uint32_t position() {
        if (!f) return 0;
        return ftell(f);
    }
    void close() {
        if (f) {
            fclose(f);
            f = nullptr;
        }
        if (dir) {
            closedir(dir);
            dir = nullptr;
        }
        valid = false;
    }

private:
    FILE* f;
    DIR* dir;
    bool valid;
    bool _isDir;
    std::string path;
    std::string fileName;
};

class SDFS {
public:
    File open(const char* path, const char* mode = "r") {
        std::string fullPath = std::string("data") + (path[0] == '/' ? "" : "/") + path;
        
        DIR* d = opendir(fullPath.c_str());
        if (d) {
            return File(d, fullPath.c_str());
        }
        
        FILE* f = fopen(fullPath.c_str(), mode);
        return File(f);
    }
};

extern SDFS SD;

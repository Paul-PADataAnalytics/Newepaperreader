#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <string>

struct FileInfo {
    std::string name;
    std::string path;
    bool isDirectory;
    size_t size;
};

class FileBrowser {
public:
    FileBrowser();
    void setRoot(const char* path);
    bool refresh();
    
    std::vector<FileInfo>& getFiles();
    void enterDirectory(const char* dirName);
    void goUp();
    
    std::string getCurrentPath() const;

private:
    std::string currentPath;
    std::vector<FileInfo> files;
    
    void loadDirectory(const char* path);
};

#ifndef NATIVE_TESTING
#include "FileBrowser.h"

FileBrowser::FileBrowser() : currentPath("/") {
}

void FileBrowser::setRoot(const char* path) {
    currentPath = path;
    refresh();
}

bool FileBrowser::refresh() {
    loadDirectory(currentPath.c_str());
    return true;
}

std::vector<FileInfo>& FileBrowser::getFiles() {
    return files;
}

void FileBrowser::enterDirectory(const char* dirName) {
    if (currentPath.back() != '/') {
        currentPath += "/";
    }
    currentPath += dirName;
    refresh();
}

void FileBrowser::goUp() {
    if (currentPath == "/") return;
    
    size_t lastSlash = currentPath.find_last_of('/');
    if (lastSlash == 0) {
        currentPath = "/";
    } else if (lastSlash != std::string::npos) {
        currentPath = currentPath.substr(0, lastSlash);
    } else {
        currentPath = "/";
    }
    refresh();
}

std::string FileBrowser::getCurrentPath() const {
    return currentPath;
}

void FileBrowser::loadDirectory(const char* path) {
    files.clear();
    
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        Serial.printf("Failed to open directory: %s\n", path);
        return;
    }
    
    File file = dir.openNextFile();
    while (file) {
        FileInfo info;
        info.name = file.name();
        
        // Ensure path correctly handles slashes
        std::string fullPath = currentPath;
        if (fullPath.back() != '/') {
            fullPath += "/";
        }
        fullPath += info.name;
        
        info.path = fullPath;
        info.isDirectory = file.isDirectory();
        info.size = file.size();
        
        // Skip hidden files (starting with dot)
        if (info.name.length() > 0 && info.name[0] != '.') {
            files.push_back(info);
        }
        
        file = dir.openNextFile();
    }
}
#endif

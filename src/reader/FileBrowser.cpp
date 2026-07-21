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
    
#ifndef NATIVE_TESTING
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        printf("FileBrowser::loadDirectory - Failed to open directory: %s\n", path);
        return;
    }
    
    printf("FileBrowser::loadDirectory - Opened directory: %s\n", path);
    File file = dir.openNextFile();
    while (file) {
        FileInfo info;
        info.name = file.name();
        
        std::string fullPath = currentPath;
        if (fullPath.back() != '/') {
            fullPath += "/";
        }
        fullPath += info.name;
        
        info.path = fullPath;
        info.isDirectory = file.isDirectory();
        info.size = file.size();
        
        if (info.name.length() > 0 && info.name[0] != '.') {
            files.push_back(info);
        }
        
        file = dir.openNextFile();
    }
#else
    // Mock for native testing
    FileInfo info1 = {"The Great Gatsby.epub", "/books/The Great Gatsby.epub", false, 1200000};
    FileInfo info2 = {"Pride and Prejudice.epub", "/books/Pride and Prejudice.epub", false, 800000};
    FileInfo info3 = {"1984.epub", "/books/1984.epub", false, 950000};
    FileInfo info4 = {"To Kill a Mockingbird.epub", "/books/To Kill a Mockingbird.epub", false, 1100000};
    
    files.push_back(info1);
    files.push_back(info2);
    files.push_back(info3);
    files.push_back(info4);
#endif
}

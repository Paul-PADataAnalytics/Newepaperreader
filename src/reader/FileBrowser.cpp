#include "FileBrowser.h"

#ifdef NATIVE_TESTING
#include <dirent.h>
#include <sys/types.h>
#endif

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
    // Use opendir for native testing
    std::string nativePath = path;
    if (nativePath.length() > 0 && nativePath[0] == '/') {
        nativePath = nativePath.substr(1);
    }
    if (nativePath.empty()) {
        nativePath = ".";
    }
    
    DIR* d = opendir(nativePath.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.length() > 0 && name[0] == '.') continue;
            
            FileInfo info;
            info.name = name;
            
            std::string fullPath = currentPath;
            if (fullPath.back() != '/') {
                fullPath += "/";
            }
            fullPath += info.name;
            info.path = fullPath;
            info.isDirectory = (entry->d_type == DT_DIR);
            
            std::string realFilePath = nativePath + "/" + name;
            FILE* f = fopen(realFilePath.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                info.size = ftell(f);
                fclose(f);
            } else {
                info.size = 0;
            }
            files.push_back(info);
        }
        closedir(d);
    }
#endif
}

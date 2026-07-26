#pragma once

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#endif
#include <vector>
#include <string>
#include <functional>


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

    // Recursively lists all files/directories under rootPath, descending into
    // every subdirectory (unlike getFiles(), which only lists one level).
    // Each returned FileInfo's `path` is the full path from the SD root
    // (e.g. "/books/scifi/foo.epub"), suitable for direct use with
    // AppStorage::toRuntimePath(). `onProgress`, if provided, is invoked once
    // per directory visited so callers can show a "scanning" UI for slow scans.
    std::vector<FileInfo> scanRecursive(const char* rootPath, std::function<void()> onProgress = nullptr, int maxDepth = 8);

private:
    std::string currentPath;
    std::vector<FileInfo> files;
    
    void loadDirectory(const char* path);
    void scanRecursiveHelper(const std::string& path, std::vector<FileInfo>& out, int depth, int maxDepth, const std::function<void()>& onProgress);
};

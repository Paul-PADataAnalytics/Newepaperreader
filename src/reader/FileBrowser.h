#pragma once

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#endif
#include <cstdint>
#include <vector>
#include <string>
#include <functional>


struct FileInfo {
    std::string name;
    std::string path;
    bool isDirectory = false;
    size_t size = 0;
    // Opaque modification stamp, only meaningful for equality checks (FAT
    // date/time on the device, st_mtime in the simulator). 0 if unknown.
    uint32_t mtime = 0;
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
    // per directory visited AND once per file entry read, so callers can show
    // live "scanning" progress even for a large flat directory with no
    // subdirectories (a single directory visit alone gives no useful signal
    // for how much of a long scan remains).
    std::vector<FileInfo> scanRecursive(const char* rootPath, std::function<void()> onProgress = nullptr, int maxDepth = 8);
    bool scanSucceeded() const { return m_scanSucceeded; }

    // Lists one directory (SD-root path such as "/books"), reporting each
    // non-hidden entry with its size and modification stamp. On the device
    // this reads FAT directory records directly, so sizes and dates cost no
    // extra per-file lookups (File::openNextFile() stats AND opens every
    // entry, which made large folders very slow to scan). Returns false if
    // the directory can't be opened.
    static bool listDirectory(const std::string& sdPath,
                              const std::function<void(const FileInfo&)>& onEntry);

    // Size and modification stamp of one file (same stamp format as
    // listDirectory()). Returns false if it doesn't exist.
    static bool fileStamp(const std::string& sdPath, uint32_t& size, uint32_t& mtime);

private:
    std::string currentPath;
    std::vector<FileInfo> files;
    bool m_scanSucceeded = true;
    
    void loadDirectory(const char* path);
    void scanRecursiveHelper(const std::string& path, std::vector<FileInfo>& out, int depth, int maxDepth, const std::function<void()>& onProgress);
};

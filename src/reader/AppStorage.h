#pragma once

#include <string>

namespace AppStorage {

// Initializes storage on embedded targets and performs a basic root listing.
bool initialize();

// Converts a browser path (e.g. /books/foo.epub) into a runtime path.
std::string toRuntimePath(const std::string& browserPath);

// Converts a runtime path into an absolute VFS path suitable for standard C file I/O (fopen, miniz).
std::string toVfsPath(const std::string& runtimePath);

// Returns the bookmark file path for a given runtime book path.
std::string toBookmarkPath(const std::string& runtimeBookPath);

// Persists bookmark offset.
bool saveBookmark(const std::string& runtimeBookPath, int offset);

// Loads bookmark offset; returns 0 when missing.
int loadBookmark(const std::string& runtimeBookPath);

// File management operations. Paths are SD-root-relative (for example,
// /books/title.epub). Existing destination files are never overwritten.
bool copyFile(const std::string& sourcePath, const std::string& destinationPath,
              std::string& error);
bool moveFile(const std::string& sourcePath, const std::string& destinationPath,
              std::string& error);
bool deleteFile(const std::string& path, std::string& error);
struct SavedSystemState {
    int appIndex = -1;
    int internalState = 0;
    bool isPortrait = false;
    std::string path = "";
    bool valid = false;
};

bool saveSystemState(const SavedSystemState& state);
SavedSystemState loadSystemState();
bool clearSystemState();

} // namespace AppStorage


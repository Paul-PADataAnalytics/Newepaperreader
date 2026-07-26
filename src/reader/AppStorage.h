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

// Persists bookmark offset. For EPUB books, chapterIndex/totalChapters must
// also be supplied (offset alone is only meaningful within a single chapter's
// text - see loadBookmarkInfo) - text/rtf/md books can omit them (offset is
// an absolute file position and needs no chapter context).
bool saveBookmark(const std::string& runtimeBookPath, int offset, int chapterIndex = 0, int totalChapters = 0);

// Loads bookmark offset; returns 0 when missing. For EPUB books this is only
// the intra-chapter offset - use loadBookmarkInfo() to also recover which
// chapter it belongs to.
int loadBookmark(const std::string& runtimeBookPath);

// Full bookmark contents, including EPUB chapter context. Fields default to
// 0 when the bookmark file is missing or was written before chapter tracking
// existed (safe/backward-compatible: chapterIndex 0 is the book's start).
struct BookmarkInfo {
    int offset = 0;
    int chapterIndex = 0;
    int totalChapters = 0;
};
BookmarkInfo loadBookmarkInfo(const std::string& runtimeBookPath);

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


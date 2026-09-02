#pragma once

#include <string>
#include <vector>
#include <cstdint>

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

// True if this boot resumed from our own deep-sleep lock mode (BUTTON_1
// EXT0 wakeup) rather than a fresh power-on/reset. The SD card is never
// unmounted across a sleep-resume, so a cached content scan is still
// trustworthy; on a fresh power-on the card could have been swapped or
// edited by another device, so callers should treat cached scans as stale.
// Always false under NATIVE_TESTING (no real sleep/wake cycle to detect).
bool wasResumedFromSleep();

// Marks every persisted content-library scan stale, forcing a rescan next
// time one is loaded even on a sleep-resume boot. Call this after any
// operation that adds/moves/deletes files in a scanned content directory
// (SD file manager operations, BLE/WiFi sync uploads, etc.) so apps never
// show a stale library after content actually changed.
void markLibraryDirty();

// One entry in a persisted recursive content scan - see saveLibraryIndex()/
// loadLibraryIndex(). Deliberately minimal (just enough to reconstruct a
// FileInfo) since it's a cache, not the source of truth.
struct LibraryIndexEntry {
    std::string path;
    uint32_t size = 0;
};

// Persists a full recursive scan of `rootPath` (e.g. "/books") so future app
// launches don't need to re-walk the SD card - one of the slowest operations
// on this hardware - unless markLibraryDirty() was called or this boot is a
// fresh power-on. Clears the dirty flag set by markLibraryDirty() on success.
bool saveLibraryIndex(const std::string& rootPath, const std::vector<LibraryIndexEntry>& entries);

struct LibraryIndex {
    std::vector<LibraryIndexEntry> entries;
    bool valid = false;
};

// Loads a previously-saved index for `rootPath`. `valid` is false (forcing
// the caller to do a real scan) when no index has been saved yet, when
// markLibraryDirty() invalidated it, or when this boot is a fresh power-on
// (see wasResumedFromSleep()) - i.e. this single call encodes the full
// "should we rescan?" policy so apps don't have to duplicate it.
LibraryIndex loadLibraryIndex(const std::string& rootPath);

} // namespace AppStorage

